/*

Jabber Protocol Plugin for Miranda NG

Copyright (c) 2002-04  Santithorn Bunchua
Copyright (c) 2005-12  George Hazan
Copyright (c) 2007     Maxim Mluhov
Copyright (�) 2012-16 Miranda NG project

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include "stdafx.h"
#include "jabber_list.h"
#include "jabber_caps.h"

///////////////////////////////////////////////////////////////////////////////
// JabberAddContactToRoster() - adds a contact to the roster

void CJabberProto::AddContactToRoster(const wchar_t *jid, const wchar_t *nick, const wchar_t *grpName)
{
	XmlNodeIq iq(L"set", SerialNext());
	HXML query = iq << XQUERY(JABBER_FEAT_IQ_ROSTER)
		<< XCHILD(L"item") << XATTR(L"jid", jid) << XATTR(L"name", nick);
	if (grpName)
		query << XCHILD(L"group", grpName);
	m_ThreadInfo->send(iq);
}

///////////////////////////////////////////////////////////////////////////////
// JabberChatDllError() - missing CHAT.DLL

void JabberChatDllError()
{
	MessageBox(NULL,
		TranslateT("Chat plugin is required for conferences. Install it before chatting"),
		TranslateT("Jabber Error"), MB_OK | MB_SETFOREGROUND);
}

///////////////////////////////////////////////////////////////////////////////
// JabberDBAddAuthRequest()

void CJabberProto::DBAddAuthRequest(const wchar_t *jid, const wchar_t *nick)
{
	MCONTACT hContact = DBCreateContact(jid, nick, true, true);
	delSetting(hContact, "Hidden");

	T2Utf szJid(jid);
	T2Utf szNick(nick);

	// blob is: uin(DWORD), hContact(DWORD), nick(ASCIIZ), first(ASCIIZ), last(ASCIIZ), email(ASCIIZ), reason(ASCIIZ)
	// blob is: 0(DWORD), hContact(DWORD), nick(ASCIIZ), ""(ASCIIZ), ""(ASCIIZ), email(ASCIIZ), ""(ASCIIZ)
	DBEVENTINFO dbei = { sizeof(DBEVENTINFO) };
	dbei.szModule = m_szModuleName;
	dbei.timestamp = (DWORD)time(NULL);
	dbei.flags = DBEF_UTF;
	dbei.eventType = EVENTTYPE_AUTHREQUEST;
	dbei.cbBlob = (DWORD)(sizeof(DWORD) * 2 + mir_strlen(szNick) + mir_strlen(szJid) + 5);
	PBYTE pCurBlob = dbei.pBlob = (PBYTE)mir_alloc(dbei.cbBlob);
	*((PDWORD)pCurBlob) = 0; pCurBlob += sizeof(DWORD);
	*((PDWORD)pCurBlob) = (DWORD)hContact; pCurBlob += sizeof(DWORD);
	mir_strcpy((char*)pCurBlob, szNick); pCurBlob += mir_strlen(szNick) + 1;
	*pCurBlob = '\0'; pCurBlob++;		//firstName
	*pCurBlob = '\0'; pCurBlob++;		//lastName
	mir_strcpy((char*)pCurBlob, szJid); pCurBlob += mir_strlen(szJid) + 1;
	*pCurBlob = '\0';					//reason

	db_event_add(NULL, &dbei);
	debugLogA("Setup DBAUTHREQUEST with nick='%s' jid='%s'", szNick, szJid);
}

///////////////////////////////////////////////////////////////////////////////
// JabberDBCreateContact()

MCONTACT CJabberProto::DBCreateContact(const wchar_t *jid, const wchar_t *nick, bool temporary, bool stripResource)
{
	if (jid == NULL || jid[0] == '\0')
		return NULL;

	MCONTACT hContact = HContactFromJID(jid, stripResource);
	if (hContact != NULL)
		return hContact;

	// strip resource if present
	wchar_t szJid[JABBER_MAX_JID_LEN];
	if (stripResource)
		JabberStripJid(jid, szJid, _countof(szJid));
	else
		wcsncpy_s(szJid, jid, _TRUNCATE);

	MCONTACT hNewContact = db_add_contact();
	Proto_AddToContact(hNewContact, m_szModuleName);
	setWString(hNewContact, "jid", szJid);
	if (nick != NULL && *nick != '\0')
		setWString(hNewContact, "Nick", nick);
	if (temporary)
		db_set_b(hNewContact, "CList", "NotOnList", 1);
	else
		SendGetVcard(szJid);
	
	if (JABBER_LIST_ITEM *pItem = ListAdd(LIST_ROSTER, jid, hNewContact))
		pItem->bUseResource = wcschr(szJid, '/') != 0;
	
	debugLogW(L"Create Jabber contact jid=%s, nick=%s", szJid, nick);
	DBCheckIsTransportedContact(szJid, hNewContact);
	return hNewContact;
}

BOOL CJabberProto::AddDbPresenceEvent(MCONTACT hContact, BYTE btEventType)
{
	if (!hContact)
		return FALSE;

	switch (btEventType) {
	case JABBER_DB_EVENT_PRESENCE_SUBSCRIBE:
	case JABBER_DB_EVENT_PRESENCE_SUBSCRIBED:
	case JABBER_DB_EVENT_PRESENCE_UNSUBSCRIBE:
	case JABBER_DB_EVENT_PRESENCE_UNSUBSCRIBED:
		if (!m_options.LogPresence)
			return FALSE;
		break;

	case JABBER_DB_EVENT_PRESENCE_ERROR:
		if (!m_options.LogPresenceErrors)
			return FALSE;
		break;
	}

	DBEVENTINFO dbei = { sizeof(dbei) };
	dbei.pBlob = &btEventType;
	dbei.cbBlob = sizeof(btEventType);
	dbei.eventType = EVENTTYPE_JABBER_PRESENCE;
	dbei.flags = DBEF_READ;
	dbei.timestamp = time(NULL);
	dbei.szModule = m_szModuleName;
	db_event_add(hContact, &dbei);

	return TRUE;
}

///////////////////////////////////////////////////////////////////////////////
// JabberGetAvatarFileName() - gets a file name for the avatar image

void CJabberProto::GetAvatarFileName(MCONTACT hContact, wchar_t* pszDest, size_t cbLen)
{
	int tPathLen = mir_snwprintf(pszDest, cbLen, L"%s\\%S", VARSW(L"%miranda_avatarcache%"), m_szModuleName);

	DWORD dwAttributes = GetFileAttributes(pszDest);
	if (dwAttributes == 0xffffffff || (dwAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
		CreateDirectoryTreeW(pszDest);

	pszDest[tPathLen++] = '\\';

	const wchar_t* szFileType = ProtoGetAvatarExtension(getByte(hContact, "AvatarType", PA_FORMAT_PNG));

	if (hContact != NULL) {
		char str[256];
		JabberShaStrBuf buf;
		DBVARIANT dbv;
		if (!db_get_utf(hContact, m_szModuleName, "jid", &dbv)) {
			strncpy_s(str, dbv.pszVal, _TRUNCATE);
			str[sizeof(str) - 1] = 0;
			db_free(&dbv);
		}
		else _i64toa((LONG_PTR)hContact, str, 10);
		mir_snwprintf(pszDest + tPathLen, MAX_PATH - tPathLen, L"%S%s", JabberSha1(str, buf), szFileType);
	}
	else if (m_ThreadInfo != NULL) {
		mir_snwprintf(pszDest + tPathLen, MAX_PATH - tPathLen, L"%s@%S avatar%s",
			m_ThreadInfo->conn.username, m_ThreadInfo->conn.server, szFileType);
	}
	else {
		ptrA res1(getStringA("LoginName")), res2(getStringA("LoginServer"));
		mir_snwprintf(pszDest + tPathLen, MAX_PATH - tPathLen, L"%S@%S avatar%s",
			(res1) ? (LPSTR)res1 : "noname", (res2) ? (LPSTR)res2 : m_szModuleName, szFileType);
	}
}

///////////////////////////////////////////////////////////////////////////////
// JabberResolveTransportNicks - massive vcard update

void CJabberProto::ResolveTransportNicks(const wchar_t *jid)
{
	// Set all contacts to offline
	MCONTACT hContact = m_ThreadInfo->resolveContact;
	if (hContact == NULL)
		hContact = db_find_first(m_szModuleName);

	for (; hContact != NULL; hContact = db_find_next(hContact, m_szModuleName)) {
		if (!getByte(hContact, "IsTransported", 0))
			continue;

		ptrW dbJid(getWStringA(hContact, "jid")); if (dbJid == NULL) continue;
		ptrW dbNick(getWStringA(hContact, "Nick")); if (dbNick == NULL) continue;

		wchar_t *p = wcschr(dbJid, '@');
		if (p == NULL)
			continue;

		*p = 0;
		if (!mir_wstrcmp(jid, p + 1) && !mir_wstrcmp(dbJid, dbNick)) {
			*p = '@';
			m_ThreadInfo->resolveID = SendGetVcard(dbJid);
			m_ThreadInfo->resolveContact = hContact;
			return;
		}
	}

	m_ThreadInfo->resolveID = -1;
	m_ThreadInfo->resolveContact = NULL;
}

///////////////////////////////////////////////////////////////////////////////
// JabberSetServerStatus()

void CJabberProto::SetServerStatus(int iNewStatus)
{
	if (!m_bJabberOnline)
		return;

	// change status
	int oldStatus = m_iStatus;
	switch (iNewStatus) {
	case ID_STATUS_ONLINE:
	case ID_STATUS_NA:
	case ID_STATUS_FREECHAT:
	case ID_STATUS_INVISIBLE:
		m_iStatus = iNewStatus;
		break;
	case ID_STATUS_AWAY:
	case ID_STATUS_ONTHEPHONE:
	case ID_STATUS_OUTTOLUNCH:
		m_iStatus = ID_STATUS_AWAY;
		break;
	case ID_STATUS_DND:
	case ID_STATUS_OCCUPIED:
		m_iStatus = ID_STATUS_DND;
		break;
	default:
		return;
	}

	if (m_iStatus == oldStatus)
		return;

	// send presence update
	SendPresence(m_iStatus, true);
	ProtoBroadcastAck(NULL, ACKTYPE_STATUS, ACKRESULT_SUCCESS, (HANDLE)oldStatus, m_iStatus);
}

// Process a string, and double all % characters, according to chat.dll's restrictions
// Returns a pointer to the new string (old one is not freed)

wchar_t* UnEscapeChatTags(wchar_t* str_in)
{
	wchar_t *s = str_in, *d = str_in;
	while (*s) {
		if (*s == '%' && s[1] == '%')
			s++;
		*d++ = *s++;
	}
	*d = 0;
	return str_in;
}

//////////////////////////////////////////////////////////////////////////
// update MirVer with data for active resource

struct
{
	wchar_t *node;
	wchar_t *name;
}
static sttCapsNodeToName_Map[] =
{
	{ L"http://miranda-im.org", L"Miranda IM Jabber" },
	{ L"http://miranda-ng.org", L"Miranda NG Jabber" },
	{ L"http://www.google.com", L"GTalk" },
	{ L"http://mail.google.com", L"GMail" },
	{ L"http://talk.google.com/xmpp/bot", L"GTalk Bot" },
	{ L"http://www.android.com", L"Android" },
};

void CJabberProto::UpdateMirVer(JABBER_LIST_ITEM *item)
{
	MCONTACT hContact = HContactFromJID(item->jid);
	if (!hContact)
		return;

	debugLogW(L"JabberUpdateMirVer: for jid %s", item->jid);

	pResourceStatus p(NULL);
	if (item->resourceMode == RSMODE_LASTSEEN)
		p = item->m_pLastSeenResource;
	else if (item->resourceMode == RSMODE_MANUAL)
		p = item->m_pManualResource;

	if (p)
		UpdateMirVer(hContact, p);
}

void CJabberProto::FormatMirVer(pResourceStatus &resource, CMStringW &res)
{
	res.Empty();
	if (resource == NULL)
		return;

	// jabber:iq:version info requested and exists?
	if (resource->m_dwVersionRequestTime && resource->m_tszSoftware) {
		debugLogW(L"JabberUpdateMirVer: for iq:version rc %s: %s", resource->m_tszResourceName, resource->m_tszSoftware);
		if (!resource->m_tszSoftwareVersion || wcsstr(resource->m_tszSoftware, resource->m_tszSoftwareVersion))
			res = resource->m_tszSoftware;
		else
			res.Format(L"%s %s", resource->m_tszSoftware, resource->m_tszSoftwareVersion);
	}
	// no version info and no caps info? set MirVer = resource name
	else if (!resource->m_tszCapsNode || !resource->m_tszCapsVer) {
		debugLogW(L"JabberUpdateMirVer: for rc %s: %s", resource->m_tszResourceName, resource->m_tszResourceName);
		if (resource->m_tszResourceName)
			res = resource->m_tszResourceName;
	}
	// XEP-0115 caps mode
	else {
		debugLogW(L"JabberUpdateMirVer: for rc %s: %s#%s", resource->m_tszResourceName, resource->m_tszCapsNode, resource->m_tszCapsVer);

		int i;

		// search through known software list
		for (i = 0; i < _countof(sttCapsNodeToName_Map); i++)
			if (wcsstr(resource->m_tszCapsNode, sttCapsNodeToName_Map[i].node)) {
				res.Format(L"%s %s", sttCapsNodeToName_Map[i].name, resource->m_tszCapsVer);
				break;
			}

		// unknown software
		if (i == _countof(sttCapsNodeToName_Map))
			res.Format(L"%s %s", resource->m_tszCapsNode, resource->m_tszCapsVer);
	}

	// attach additional info for fingerprint plguin
	if (resource->m_tszCapsExt && wcsstr(resource->m_tszCapsExt, JABBER_EXT_PLATFORMX86) && !wcsstr(res, L"x86"))
		res.Append(L" x86");

	if (resource->m_tszCapsExt && wcsstr(resource->m_tszCapsExt, JABBER_EXT_PLATFORMX64) && !wcsstr(res, L"x64"))
		res.Append(L" x64");

	if (resource->m_tszCapsExt && wcsstr(resource->m_tszCapsExt, JABBER_EXT_SECUREIM) && !wcsstr(res, L"(SecureIM)"))
		res.Append(L" (SecureIM)");

	if (resource->m_tszCapsExt && wcsstr(resource->m_tszCapsExt, JABBER_EXT_MIROTR) && !wcsstr(res, L"(MirOTR)"))
		res.Append(L" (MirOTR)");

	if (resource->m_tszCapsExt && wcsstr(resource->m_tszCapsExt, JABBER_EXT_NEWGPG) && !wcsstr(res, L"(New_GPG)"))
		res.Append(L" (New_GPG)");

	if (resource->m_tszResourceName && !wcsstr(res, resource->m_tszResourceName))
		if (wcsstr(res, L"Miranda IM") || wcsstr(res, L"Miranda NG") || m_options.ShowForeignResourceInMirVer)
			res.AppendFormat(L" [%s]", resource->m_tszResourceName);
}


void CJabberProto::UpdateMirVer(MCONTACT hContact, pResourceStatus &resource)
{
	CMStringW tszMirVer;
	FormatMirVer(resource, tszMirVer);
	if (!tszMirVer.IsEmpty())
		setWString(hContact, "MirVer", tszMirVer);

	ptrW jid(getWStringA(hContact, "jid"));
	if (jid == NULL)
		return;

	wchar_t szFullJid[JABBER_MAX_JID_LEN];
	if (resource->m_tszResourceName && !wcschr(jid, '/'))
		mir_snwprintf(szFullJid, L"%s/%s", jid, resource->m_tszResourceName);
	else
		mir_wstrncpy(szFullJid, jid, _countof(szFullJid));
	setWString(hContact, DBSETTING_DISPLAY_UID, szFullJid);
}

void CJabberProto::UpdateSubscriptionInfo(MCONTACT hContact, JABBER_LIST_ITEM *item)
{
	switch (item->subscription) {
	case SUB_TO:
		setWString(hContact, "SubscriptionText", TranslateT("To"));
		setString(hContact, "Subscription", "to");
		setByte(hContact, "Auth", 0);
		setByte(hContact, "Grant", 1);
		break;
	case SUB_FROM:
		setWString(hContact, "SubscriptionText", TranslateT("From"));
		setString(hContact, "Subscription", "from");
		setByte(hContact, "Auth", 1);
		setByte(hContact, "Grant", 0);
		break;
	case SUB_BOTH:
		setWString(hContact, "SubscriptionText", TranslateT("Both"));
		setString(hContact, "Subscription", "both");
		setByte(hContact, "Auth", 0);
		setByte(hContact, "Grant", 0);
		break;
	case SUB_NONE:
		setWString(hContact, "SubscriptionText", TranslateT("None"));
		setString(hContact, "Subscription", "none");
		setByte(hContact, "Auth", 1);
		setByte(hContact, "Grant", 1);
		break;
	}
}

void CJabberProto::SetContactOfflineStatus(MCONTACT hContact)
{
	if (getWord(hContact, "Status", ID_STATUS_OFFLINE) != ID_STATUS_OFFLINE)
		setWord(hContact, "Status", ID_STATUS_OFFLINE);

	delSetting(hContact, DBSETTING_XSTATUSID);
	delSetting(hContact, DBSETTING_XSTATUSNAME);
	delSetting(hContact, DBSETTING_XSTATUSMSG);
	delSetting(hContact, DBSETTING_DISPLAY_UID);

	ResetAdvStatus(hContact, ADVSTATUS_MOOD);
	ResetAdvStatus(hContact, ADVSTATUS_TUNE);
}

void CJabberProto::InitPopups(void)
{
	wchar_t desc[256];
	mir_snwprintf(desc, L"%s %s", m_tszUserName, TranslateT("Errors"));

	char name[256];
	mir_snprintf(name, "%s_%s", m_szModuleName, "Error");

	POPUPCLASS ppc = { sizeof(ppc) };
	ppc.flags = PCF_TCHAR;
	ppc.pwszDescription = desc;
	ppc.pszName = name;
	ppc.hIcon = LoadIconEx("main");
	ppc.colorBack = RGB(191, 0, 0); //Red
	ppc.colorText = RGB(255, 245, 225); //Yellow
	ppc.iSeconds = 60;
	m_hPopupClass = Popup_RegisterClass(&ppc);

	IcoLib_ReleaseIcon(ppc.hIcon);
}

void CJabberProto::MsgPopup(MCONTACT hContact, const wchar_t *szMsg, const wchar_t *szTitle)
{
	if (ServiceExists(MS_POPUP_ADDPOPUPCLASS)) {
		char name[256];

		POPUPDATACLASS ppd = { sizeof(ppd) };
		ppd.pwszTitle = szTitle;
		ppd.pwszText = szMsg;
		ppd.pszClassName = name;
		ppd.hContact = hContact;
		mir_snprintf(name, "%s_%s", m_szModuleName, "Error");

		CallService(MS_POPUP_ADDPOPUPCLASS, 0, (LPARAM)&ppd);
	}
	else {
		DWORD mtype = MB_OK | MB_SETFOREGROUND | MB_ICONSTOP;
		MessageBox(NULL, szMsg, szTitle, mtype);
	}
}

CMStringW CJabberProto::ExtractImage(HXML node)
{
	HXML nHtml, nBody, nImg;
	LPCTSTR src;
	CMStringW link;

	if ((nHtml = XmlGetChild(node, "html")) != NULL &&
		(nBody = XmlGetChild(nHtml, "body")) != NULL &&
		(nImg = XmlGetChild(nBody, "img")) != NULL &&
		(src = XmlGetAttrValue(nImg, L"src")) != NULL) {

		CMStringW strSrc(src);
		if (strSrc.Left(11).Compare(L"data:image/") == 0) {
			int end = strSrc.Find(L';');
			if (end != -1) {
				CMStringW ext(strSrc.c_str() + 11, end - 11);
				int comma = strSrc.Find(L',', end);
				if (comma != -1) {
					CMStringW image(strSrc.c_str() + comma + 1, strSrc.GetLength() - comma - 1);
					image.Replace(L"%2B", L"+");
					image.Replace(L"%2F", L"/");
					image.Replace(L"%3D", L"=");

					wchar_t tszTempPath[MAX_PATH], tszTempFile[MAX_PATH];
					GetTempPath(_countof(tszTempPath), tszTempPath);
					GetTempFileName(tszTempPath, L"jab", InterlockedIncrement(&g_nTempFileId), tszTempFile);
					wcsncat_s(tszTempFile, L".", 1);
					wcsncat_s(tszTempFile, ext, ext.GetLength());

					HANDLE h = CreateFile(tszTempFile, GENERIC_READ | GENERIC_WRITE,
						FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, CREATE_ALWAYS,
						FILE_ATTRIBUTE_NORMAL, NULL);

					if (h != INVALID_HANDLE_VALUE) {
						DWORD n;
						unsigned int bufferLen;
						ptrA buffer((char*)mir_base64_decode(_T2A(image), &bufferLen));
						WriteFile(h, buffer, bufferLen, &n, NULL);
						CloseHandle(h);

						link = L" file:///";
						link += tszTempFile;
					}
				}
			}
		}
	}
	return link;
}
