/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 *
 * The contents of this file are subject to the Netscape Public License
 * Version 1.0 (the "NPL"); you may not use this file except in
 * compliance with the NPL.  You may obtain a copy of the NPL at
 * http://www.mozilla.org/NPL/
 *
 * Software distributed under the NPL is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the NPL
 * for the specific language governing rights and limitations under the
 * NPL.
 *
 * The Initial Developer of this code under the NPL is Netscape
 * Communications Corporation.  Portions created by Netscape are
 * Copyright (C) 1998 Netscape Communications Corporation.  All Rights
 * Reserved.
 */

#define FORCE_PR_LOG /* Allow logging in the release build (sorry this breaks the PCH) */
#include "msgCore.h"    // precompiled header...
#include "MailNewsTypes.h"
#include "nntpCore.h"

#include "nsNNTPProtocol.h"
#include "nsINNTPArticleList.h"
#include "nsIOutputStream.h"
#include "nsIInputStream.h"
#include "nsFileStream.h"

#include "nsCOMPtr.h"

// mscott: this is a short term hack for the demo...we should be able to remove this webshell
// dependency at a later date....
#include "nsIWebShell.h"

#include "nsINetService.h"

#include "nsMsgBaseCID.h"
#include "nsMsgNewsCID.h"

#include "nsINntpUrl.h"

#include "nsCRT.h"

#include "rosetta.h"
#include HG40855

#include "prtime.h"
#include "prlog.h"
#include "prerror.h"
#include "nsEscape.h"

#include "prprf.h"
#include "merrors.h"

/* include event sink interfaces for news */

#include "nsIMsgHeaderParser.h" 

#include "nsINntpUrl.h"
#include "nsNNTPHost.h"
#include "nsMsgKeySet.h"

#include "nsNewsUtils.h"
#include "nsIPref.h"

#include "nsIMsgMailSession.h"
#include "nsIMsgIdentity.h"
#include "nsINetSupportDialogService.h"

#define PREF_NEWS_MAX_ARTICLES "news.max_articles"
#define PREF_NEWS_MARK_OLD_READ "news.mark_old_read"

#define DEFAULT_NEWS_CHUNK_SIZE -1

#ifdef DEBUG_sspitzer_
#define DEBUG_NEWS 1
#endif


// move these to a string bundle
#define UNTIL_STRING_BUNDLES_XP_HTML_NEWS_ERROR "<TITLE>Error!</TITLE>\n<H1>Error!</H1> newsgroup server responded: <b>%.256s</b><p>\n"
#define UNTIL_STRING_BUNDLES_XP_HTML_ARTICLE_EXPIRED "<b><p>Perhaps the article has expired</b><p>\n"
#define UNTIL_STRING_BUNDLES_XP_LIST_IDS_URL_TEXT "Click here to remove all expired articles"
#define UNTIL_STRING_BUNDLES_MK_NNTP_CANCEL_DISALLOWED "This message does not appear to be from you.  You may only cancel your own posts, not those made by others."
#define UNTIL_STRING_BUNDLES_MK_NNTP_CANCEL_CONFIRM "Are you sure you want to cancel this message?"
#define UNTIL_STRING_BUNDLES_MK_MSG_MESSAGE_CANCELLED "Message cancelled."

/* #define UNREADY_CODE	*/  /* mscott: generic flag for hiding access to url struct and active entry which are now gone */

/*#define CACHE_NEWSGRP_PASSWORD*/

extern "C"
{
char * NET_SACopy (char **destination, const char *source);
char * NET_SACat (char **destination, const char *source);

}

static NS_DEFINE_IID(kIWebShell, NS_IWEB_SHELL_IID);  
static NS_DEFINE_CID(kPrefServiceCID, NS_PREF_CID);
static NS_DEFINE_CID(kCHeaderParserCID, NS_MSGHEADERPARSER_CID);
static NS_DEFINE_CID(kNNTPArticleListCID, NS_NNTPARTICLELIST_CID);
static NS_DEFINE_CID(kNNTPHostCID, NS_NNTPHOST_CID);
static NS_DEFINE_CID(kCMsgMailSessionCID, NS_MSGMAILSESSION_CID);
static NS_DEFINE_CID(kCNetSupportDialogCID, NS_NETSUPPORTDIALOG_CID);

// quiet compiler warnings by defining these function prototypes
char *NET_ExplainErrorDetails (int code, ...);
char *MSG_UnEscapeSearchUrl (const char *commandSpecificData);

/* Logging stuff */

PRLogModuleInfo* NNTP = NULL;
#define out     PR_LOG_ALWAYS

#define NNTP_LOG_READ(buf) \
if (NNTP==NULL) \
    NNTP = PR_NewLogModule("NNTP"); \
PR_LOG(NNTP, out, ("Receiving: %s", buf)) ;

#define NNTP_LOG_WRITE(buf) \
if (NNTP==NULL) \
    NNTP = PR_NewLogModule("NNTP"); \
PR_LOG(NNTP, out, ("Sending: %s", buf)) ;

#define NNTP_LOG_NOTE(buf) \
if (NNTP==NULL) \
    NNTP = PR_NewLogModule("NNTP"); \
PR_LOG(NNTP, out, buf) ;

#ifdef DEBUG
char *stateLabels[] = {
"NNTP_RESPONSE",
#ifdef BLOCK_UNTIL_AVAILABLE_CONNECTION
"NNTP_BLOCK_UNTIL_CONNECTIONS_ARE_AVAILABLE",
"NNTP_CONNECTIONS_ARE_AVAILABLE",
#endif
"NNTP_CONNECT",
"NNTP_CONNECT_WAIT",
HG25430
"NNTP_LOGIN_RESPONSE",
"NNTP_SEND_MODE_READER",
"NNTP_SEND_MODE_READER_RESPONSE",
"SEND_LIST_EXTENSIONS",
"SEND_LIST_EXTENSIONS_RESPONSE",
"SEND_LIST_SEARCHES",
"SEND_LIST_SEARCHES_RESPONSE",
"NNTP_LIST_SEARCH_HEADERS",
"NNTP_LIST_SEARCH_HEADERS_RESPONSE",
"NNTP_GET_PROPERTIES",
"NNTP_GET_PROPERTIES_RESPONSE",
"SEND_LIST_SUBSCRIPTIONS",
"SEND_LIST_SUBSCRIPTIONS_RESPONSE",
"SEND_FIRST_NNTP_COMMAND",
"SEND_FIRST_NNTP_COMMAND_RESPONSE",
"SETUP_NEWS_STREAM",
"NNTP_BEGIN_AUTHORIZE",
"NNTP_AUTHORIZE_RESPONSE",
"NNTP_PASSWORD_RESPONSE",
"NNTP_READ_LIST_BEGIN",
"NNTP_READ_LIST",
"DISPLAY_NEWSGROUPS",
"NNTP_NEWGROUPS_BEGIN",
"NNTP_NEWGROUPS",
"NNTP_BEGIN_ARTICLE",
"NNTP_READ_ARTICLE",
"NNTP_XOVER_BEGIN",
"NNTP_FIGURE_NEXT_CHUNK",
"NNTP_XOVER_SEND",
"NNTP_XOVER_RESPONSE",
"NNTP_XOVER",
"NEWS_PROCESS_XOVER",
"NNTP_READ_GROUP",
"NNTP_READ_GROUP_RESPONSE",
"NNTP_READ_GROUP_BODY",
"NNTP_SEND_GROUP_FOR_ARTICLE",
"NNTP_SEND_GROUP_FOR_ARTICLE_RESPONSE",
"NNTP_PROFILE_ADD",
"NNTP_PROFILE_ADD_RESPONSE",
"NNTP_PROFILE_DELETE",
"NNTP_PROFILE_DELETE_RESPONSE",
"NNTP_SEND_ARTICLE_NUMBER",
"NEWS_PROCESS_BODIES",
"NNTP_PRINT_ARTICLE_HEADERS",
"NNTP_SEND_POST_DATA",
"NNTP_SEND_POST_DATA_RESPONSE",
"NNTP_CHECK_FOR_MESSAGE",
"NEWS_NEWS_RC_POST",
"NEWS_DISPLAY_NEWS_RC",
"NEWS_DISPLAY_NEWS_RC_RESPONSE",
"NEWS_START_CANCEL",
"NEWS_DO_CANCEL",
"NNTP_XPAT_SEND",
"NNTP_XPAT_RESPONSE",
"NNTP_SEARCH",
"NNTP_SEARCH_RESPONSE",
"NNTP_SEARCH_RESULTS",
"NNTP_LIST_PRETTY_NAMES",
"NNTP_LIST_PRETTY_NAMES_RESPONSE",
"NNTP_LIST_XACTIVE_RESPONSE",
"NNTP_LIST_XACTIVE",
"NNTP_LIST_GROUP",
"NNTP_LIST_GROUP_RESPONSE",
"NEWS_DONE",
"NEWS_POST_DONE",
"NEWS_ERROR",
"NNTP_ERROR",
"NEWS_FREE"
};
#endif


/* end logging */

/* Forward declarations */

#define LIST_WANTED     0
#define ARTICLE_WANTED  1
#define CANCEL_WANTED   2
#define GROUP_WANTED    3
#define NEWS_POST       4
#define READ_NEWS_RC    5
#define NEW_GROUPS      6
#define SEARCH_WANTED   7
#define PRETTY_NAMES_WANTED 8
#define PROFILE_WANTED	9
#define IDS_WANTED		10

/* the output_buffer_size must be larger than the largest possible line
 * 2000 seems good for news
 *
 * jwz: I increased this to 4k since it must be big enough to hold the
 * entire button-bar HTML, and with the new "mailto" format, that can
 * contain arbitrarily long header fields like "references".
 *
 * fortezza: proxy auth is huge, buffer increased to 8k (sigh).
 */
#define OUTPUT_BUFFER_SIZE (4096*2)

/* the amount of time to subtract from the machine time
 * for the newgroup command sent to the nntp server
 */
#define NEWGROUPS_TIME_OFFSET 60L*60L*12L   /* 12 hours */

/* Allow the user to open at most this many connections to one news host*/
#define kMaxConnectionsPerHost 3

/* Keep this many connections cached. The cache is global, not per host */
#define kMaxCachedConnections 2

/* globals */
/* mscott: I wonder if we still need these? I'd like to abstract them out into a NNTP protocol manager
   (the object that is going to manage the NNTP connections. it would keep track of the connection list.)
*/
/* PRIVATE XP_List * nntp_connection_list=0; */
PRIVATE PRBool net_news_last_username_probably_valid=PR_FALSE;
PRInt32 net_NewsChunkSize=DEFAULT_NEWS_CHUNK_SIZE; 
/* PRIVATE PRInt32 net_news_timeout = 170; */
/* seconds that an idle NNTP conn can live */

static char * last_password = 0;
static char * last_password_hostname = 0;
static char * last_username=0;
static char * last_username_hostname=0;

/* end of globals I'd like to move somewhere else */


////////////////////////////////////////////////////////////////////////////////////////////
// TEMPORARY HARD CODED FUNCTIONS 
///////////////////////////////////////////////////////////////////////////////////////////
#ifdef XP_WIN
char *XP_AppCodeName = "Mozilla";
#else
const char *XP_AppCodeName = "Mozilla";
#endif
#define NET_IS_SPACE(x) ((((unsigned int) (x)) > 0x7f) ? 0 : isspace(x))

/*
 * This function takes an error code and associated error data
 * and creates a string containing a textual description of
 * what the error is and why it happened.
 *
 * The returned string is allocated and thus should be freed
 * once it has been used.
 *
 * This function is defined in mkmessag.c.
 */
char * NET_ExplainErrorDetails (int code, ...)
{
	char * rv = PR_smprintf("%s", "Error descriptions not implemented yet");
	return rv;
}

char * NET_SACopy (char **destination, const char *source)
{
	if(*destination)
	  {
	    PR_Free(*destination);
		*destination = 0;
	  }
    if (! source)
	  {
        *destination = NULL;
	  }
    else 
	  {
        *destination = (char *) PR_Malloc (PL_strlen(source) + 1);
        if (*destination == NULL) 
 	        return(NULL);

        PL_strcpy (*destination, source);
      }
    return *destination;
}

/*  Again like strdup but it concatinates and free's and uses Realloc
*/
char * NET_SACat (char **destination, const char *source)
{
    if (source && *source)
      {
        if (*destination)
          {
            int length = PL_strlen (*destination);
            *destination = (char *) PR_Realloc (*destination, length + PL_strlen(source) + 1);
            if (*destination == NULL)
            return(NULL);

            PL_strcpy (*destination + length, source);
          }
        else
          {
            *destination = (char *) PR_Malloc (PL_strlen(source) + 1);
            if (*destination == NULL)
                return(NULL);

             PL_strcpy (*destination, source);
          }
      }
    return *destination;
}

char *MSG_UnEscapeSearchUrl (const char *commandSpecificData)
{
	char *result = (char*) PR_Malloc (PL_strlen(commandSpecificData) + 1);
	if (result)
	{
		char *resultPtr = result;
		while (1)
		{
			char ch = *commandSpecificData++;
			if (!ch)
				break;
			if (ch == '\\')
			{
				char scratchBuf[3];
				scratchBuf[0] = (char) *commandSpecificData++;
				scratchBuf[1] = (char) *commandSpecificData++;
				scratchBuf[2] = '\0';
				int accum = 0;
				PR_sscanf(scratchBuf, "%X", &accum);
				*resultPtr++ = (char) accum;
			}
			else
				*resultPtr++ = ch;
		}
		*resultPtr = '\0';
	}
	return result;
}

////////////////////////////////////////////////////////////////////////////////////////////
// END OF TEMPORARY HARD CODED FUNCTIONS 
///////////////////////////////////////////////////////////////////////////////////////////


nsNNTPProtocol::nsNNTPProtocol() : m_tempArticleFile(ARTICLE_PATH), m_tempErrorFile(ERROR_PATH)
{
	m_messageID = nsnull;
	m_cancelFromHdr = nsnull;
	m_cancelNewsgroups = nsnull;
	m_cancelDistribution = nsnull;
	m_cancelID = nsnull;
	m_cancelMessageFile = nsnull;
}

nsNNTPProtocol::~nsNNTPProtocol()
{
	delete m_lineStreamBuffer;
}

nsresult nsNNTPProtocol::Initialize(nsIURL * aURL)
{
    nsresult rv = NS_OK;
	if (!aURL)
		return NS_ERROR_NULL_POINTER;
    
    NS_WITH_SERVICE(nsIPref, prefs, kPrefServiceCID, &rv);
    if (NS_SUCCEEDED(rv) && prefs) {
        rv = prefs->GetIntPref(PREF_NEWS_MAX_ARTICLES, &net_NewsChunkSize);
        if (NS_FAILED(rv)) {
            net_NewsChunkSize = DEFAULT_NEWS_CHUNK_SIZE;
        }
    }
    else {
        return rv;
    }

	NS_PRECONDITION(aURL, "invalid URL passed into NNTP Protocol");

	// Right now, we haven't written an nsNNTPURL yet. When we do, we'll pull the event sink
	// data out of it and set our event sink member variables from it. For now, just set them
	// to NULL. 
	
	// query the URL for a nsINNTPUrl
   
	rv = aURL->QueryInterface(nsINntpUrl::GetIID(), getter_AddRefs(m_runningURL));
	if (NS_SUCCEEDED(rv) && m_runningURL)
	{
		// okay, now fill in our event sinks...Note that each getter ref counts before
		// it returns the interface to us...we'll release when we are done
		m_runningURL->GetNewsgroupList(getter_AddRefs(m_newsgroupList));
		m_runningURL->GetNntpArticleList(getter_AddRefs(m_articleList));
		m_runningURL->GetNntpHost(getter_AddRefs(m_newsHost));
		m_runningURL->GetNewsgroup(getter_AddRefs(m_newsgroup));
		m_runningURL->GetOfflineNewsState(getter_AddRefs(m_offlineNewsState));
	}
    else {
      return rv;
    }
	
	const char * hostName = NULL;
	aURL->GetHost(&hostName);
	if (hostName)
		m_hostName = nsCRT::strdup(hostName);
	else
		m_hostName = NULL;

	PRUint32 port = NEWS_PORT;
	aURL->GetHostPort(&port);

	// call base class to set up the transport
	rv = OpenNetworkSocket(aURL, port, m_hostName);

	m_dataBuf = (char *) PR_Malloc(sizeof(char) * OUTPUT_BUFFER_SIZE);
	m_dataBufSize = OUTPUT_BUFFER_SIZE;

	m_lineStreamBuffer = new nsMsgLineStreamBuffer(OUTPUT_BUFFER_SIZE, CRLF, PR_TRUE /* create new lines */);

	m_nextState = SEND_FIRST_NNTP_COMMAND;
	m_nextStateAfterResponse = NNTP_CONNECT;
	m_typeWanted = 0;
	m_responseCode = 0;
	m_previousResponseCode = 0;
	m_responseText = nsnull;

	m_path = NULL;
	m_currentGroup = NULL;
	m_firstArticle = 0;
	m_lastArticle = 0;
	m_firstPossibleArticle = 0;
	m_lastPossibleArticle = 0;
	m_numArticlesLoaded = 0;
	m_numArticlesWanted = 0;

	m_newsRCListIndex = 0;
	m_newsRCListCount = 0;
	
	PR_FREEIF(m_messageID);
	m_messageID = nsnull;

	m_articleNumber = 0;
	m_originalContentLength = 0;
	m_cancelID = nsnull;
	m_cancelFromHdr = nsnull;
	m_cancelNewsgroups = nsnull;
	m_cancelDistribution = nsnull;
	return NS_OK;
}

nsresult nsNNTPProtocol::LoadUrl(nsIURL * aURL, nsISupports * aConsumer)
{
  PRBool bVal = FALSE;
  char *hostAndPort = nsnull;
  char *group = nsnull;
  char *commandSpecificData = nsnull;
  PRBool cancel = FALSE;
  nsCOMPtr <nsINNTPNewsgroupPost> message;
  nsresult rv = NS_OK;

  m_articleNumber = -1;

  // Query the url for its nsINntpUrl interface...assert and fail to load if they passed us a non news url...

  if (aConsumer) // did the caller pass in a display stream?
	  rv = aConsumer->QueryInterface(kIWebShell, getter_AddRefs(m_displayConsumer));

  if (aURL)
  {
	  m_runningURL = do_QueryInterface(aURL);

	  // okay, now fill in our event sinks...Note that each getter ref counts before
	  // it returns the interface to us...we'll release when we are done
	  m_runningURL->GetNewsgroupList(getter_AddRefs(m_newsgroupList));
	  m_runningURL->GetNntpArticleList(getter_AddRefs(m_articleList));
	  m_runningURL->GetNntpHost(getter_AddRefs(m_newsHost));
	  m_runningURL->GetNewsgroup(getter_AddRefs(m_newsgroup));
	  m_runningURL->GetOfflineNewsState(getter_AddRefs(m_offlineNewsState));
  }
  else
	  rv = NS_ERROR_FAILURE;

  if (NS_FAILED(rv))
  {
	  goto FAIL;
  }

  PR_FREEIF(m_messageID);
  m_messageID = nsnull;
  rv = ParseURL(aURL, &hostAndPort, &bVal, &group, &m_messageID, &commandSpecificData);

  // if we don't have a news host already, go get one...
  if (!m_newsHost)
  {
      char *colon = nsnull;
      PRUint32 port = 0;
 
      colon = PL_strchr (hostAndPort, ':');
      if (colon) {
          *colon = '\0';  /* turn hostname:port into hostname */
          port = (PRUint32) strtol(colon + 1, (char **)nsnull, 10 /* base 10 */);
      }
	  rv = nsComponentManager::CreateInstance(kNNTPHostCID,
                                            nsnull,
                                            nsINNTPHost::GetIID(),
                                            getter_AddRefs(m_newsHost));
                          
      if (NS_FAILED(rv) || (!m_newsHost)) 
          goto FAIL;

      // at this point, hostAndPort is really just the hostname
      // because we put a '\0' in for the colon, if there was one
      m_newsHost->Initialize(hostAndPort /* really just hostname */, port ? port : NEWS_PORT);

	  // save it on our url for future use....
	  m_runningURL->SetNntpHost(m_newsHost);

	  // read in the newsrc file now, to build up the host correctly.
      char *newshosturi = PR_smprintf("%s/%s", kNewsRootURI, hostAndPort /* really just hostname */);
	  rv = m_newsHost->LoadNewsrc(newshosturi);
      PR_FREEIF(newshosturi);

	  if (NS_FAILED(rv))
			goto FAIL;
  }

  if (NS_FAILED(rv)) 
	goto FAIL;

  if (m_messageID && commandSpecificData && !PL_strcmp (commandSpecificData, "?cancel"))
	cancel = TRUE;

  NET_SACopy(&m_path, m_messageID);

  /* make sure the user has a news host configured */
#if UNREADY_CODE
  if (cancel /* && !ce->URL_s->internal_url */)
  {
	  /* Don't allow manual "news:ID?cancel" URLs, only internal ones. */
	  *status = -1;
	  goto FAIL;
  }
  else
#endif
  /* We are posting a user-written message
     if and only if this message has a message to post
     Cancel messages are created later with a ?cancel URL
  */
  rv = m_runningURL->GetMessageToPost(getter_AddRefs(message));
  if (NS_SUCCEEDED(rv) && message)
	{
#ifdef UNREADY_CODE
	  /* news://HOST done with a POST instead of a GET;
		 this means a new message is being posted.
		 Don't allow this unless it's an internal URL.
	   */
	  if (!ce->URL_s->internal_url)
		{
		  *status = -1;
		  goto FAIL;
		}
#endif
      /*	  PR_ASSERT (!group && !message_id && !commandSpecificData); */
	  m_typeWanted = NEWS_POST;
	  NET_SACopy(&m_path, "");
	}
  else 
	if (m_messageID)
	{
	  /* news:MESSAGE_ID
		 news:/GROUP/MESSAGE_ID (useless)
		 news://HOST/MESSAGE_ID
		 news://HOST/GROUP/MESSAGE_ID (useless)
	   */
	  if (cancel)
		m_typeWanted = CANCEL_WANTED;
	  else
		m_typeWanted = ARTICLE_WANTED;
	}
  else if (commandSpecificData)
	{
	  if (PL_strstr (commandSpecificData, "?newgroups"))
	    /* news://HOST/?newsgroups
	        news:/GROUP/?newsgroups (useless)
	        news:?newsgroups (default host)
	     */
	    m_typeWanted = NEW_GROUPS;
      else
	  {
		if (PL_strstr(commandSpecificData, "?list-pretty"))
		{
			m_typeWanted = PRETTY_NAMES_WANTED;
			m_commandSpecificData = nsCRT::strdup(commandSpecificData);
		}
		else if (PL_strstr(commandSpecificData, "?profile"))
		{
			m_typeWanted = PROFILE_WANTED;
			m_commandSpecificData = nsCRT::strdup(commandSpecificData);
		}
		else if (PL_strstr(commandSpecificData, "?list-ids"))
		{
			m_typeWanted= IDS_WANTED;
			m_commandSpecificData = nsCRT::strdup(commandSpecificData);
		}
		else
		{
			m_typeWanted = SEARCH_WANTED;
			m_commandSpecificData = nsCRT::strdup(commandSpecificData);
			m_searchData = m_commandSpecificData;
		}
	  }
	}
  else if (group)
	{
	  /* news:GROUP
		 news:/GROUP
		 news://HOST/GROUP
	   */
#ifdef UNREADY_CODE
	  if (ce->window_id->type != MWContextNews && ce->window_id->type != MWContextNewsMsg 
		  && ce->window_id->type != MWContextMailMsg
		  && ce->window_id->type != MWContextMail && ce->window_id->type != MWContextMailNewsProgress)
		{
		  *status = -1;
		  goto FAIL;
		}
#endif
	  if (PL_strchr (group, '*'))
		m_typeWanted = LIST_WANTED;
	  else
		m_typeWanted = GROUP_WANTED;
	}
  else
	{
	  /* news:
	     news://HOST
	   */
	  m_typeWanted = READ_NEWS_RC;
	}

#ifdef UNREADY_CODE
  /* let's look for the user name and password in the url */
  {
		char * unamePwd = NET_ParseURL(url, GET_USERNAME_PART | GET_PASSWORD_PART);
		char *userName,*colon,*password=NULL;

		/* get the username & password out of the combo string */
		if( (colon = PL_strchr(unamePwd, ':')) != NULL ) 
		{
			*colon = '\0';
			userName = nsCRT::strdup(unamePwd);
			password = nsCRT::strdup(colon+1);
			*colon = ':';
			PR_FREEIF(unamePwd);
		}
		else 
		{
			userName = unamePwd;
		}
		if (userName)
		{
		    char *mungedUsername = HG64643(userName);
            cd->newsgroup->SetUsername(mungedUsername);
			PR_FREEIF(mungedUsername);
			PR_FREEIF(userName);
		}
		if (password)
		{
		    char *mungedPassword = HG64643(password);
            cd->newsgroup->SetPassword(mungedPassword);

			PR_FREEIF(mungedPassword);
			PR_FREEIF(password);
		}

    }
#endif
  /* At this point, we're all done parsing the URL, and know exactly
	 what we want to do with it.
   */
#ifdef UNREADY_CODE
#ifndef NO_ARTICLE_CACHEING
  /* Turn off caching on all news entities, except articles. */
  /* It's very important that this be turned off for CANCEL_WANTED;
	 for the others, I don't know what cacheing would cause, but
	 it could only do harm, not good. */
  if(m_typeWanted != ARTICLE_WANTED)
#endif
  	ce->format_out = CLEAR_CACHE_BIT (ce->format_out);
#endif

#ifdef UNREADY_CODE
  if (m_typeWanted == ARTICLE_WANTED)
  {
		PRUint32 number = 0;
        nsresult rv;
        PRBool articleIsOffline;
        rv = m_newsgroup->IsOfflineArticle(number,&articleIsOffline);

		if (NET_IsOffline() || (NS_SUCCEEDED(rv) && articleIsOffline))
		{
			ce->local_file = TRUE;
			cd->articleIsOffline = articleIsOffline;
			ce->socket = NULL;

			if (!articleIsOffline)
				ce->format_out = CLEAR_CACHE_BIT(ce->format_out);
			NET_SetCallNetlibAllTheTime(ce->window_id,"mknews");
            
            rv = nsComponentManager::CreateInstance(kMsgOfflineNewsStateCID,
                                            nsnull,
                                            nsIMsgOfflineNewsState::GetIID(),
                                            getter_AddRefs(cd->offline_state));

            if (NS_FAILED(rv) || ! cd->offline_state) {
                goto FAIL;
            }

            rv = cd->offline_state->Initialize(cd->newsgroup, number);
            if (NS_FAILED(rv)) goto FAIL;
		}
  }
  if (cd->offline_state)
	  goto FAIL;	/* we don't need to do any of this connection stuff */
#endif


  m_nextState = SEND_FIRST_NNTP_COMMAND;

 FAIL:

  PR_FREEIF (hostAndPort);
  PR_FREEIF (group);
  PR_FREEIF (commandSpecificData);

  if (NS_FAILED(rv))
  {
#ifdef UNREADY_CODE
	  ce->URL_s->error_msg = NET_ExplainErrorDetails(*status);
#endif
      return rv;
  }
  else 
  {
	  // our first state is a process state so drive the state machine...
	  PRBool transportOpen = PR_FALSE;
	  rv = m_transport->IsTransportOpen(&transportOpen);
	  m_runningURL->SetUrlState(PR_TRUE, NS_OK); // set the url as a url currently being run...
	  if (!transportOpen)
	  {
		  m_nextStateAfterResponse = m_nextState;
		  m_nextState = NNTP_RESPONSE; 
		  rv = m_transport->Open(m_runningURL);  // opening the url will cause to get notified when the connection is established
	  }
	  else  // the connection is already open so we should begin processing our new url...
		 return ProcessProtocolState(m_runningURL, nsnull, 0); 
  }

  return rv;

}

// stop binding is a "notification" informing us that the stream associated with aURL is going away. 
NS_IMETHODIMP nsNNTPProtocol::OnStopBinding(nsIURL* aURL, nsresult aStatus, const PRUnichar* aMsg)
{
	nsMsgProtocol::OnStopBinding(aURL, aStatus, aMsg);

	// okay, we've been told that the send is done and the connection is going away. So 
	// we need to release all of our state
	return CloseSocket();
}

/* The main news load init routine, and URL parser.
   The syntax of news URLs is:

	 To list all hosts:
	   news:

	 To list all groups on a host, or to post a new message:
	   news://HOST

	 To list some articles in a group:
	   news:GROUP
	   news:/GROUP
	   news://HOST/GROUP

	 To list a particular range of articles in a group:
	   news:GROUP/N1-N2
	   news:/GROUP/N1-N2
	   news://HOST/GROUP/N1-N2

	 To retrieve an article:
	   news:MESSAGE-ID                (message-id must contain "@")

    To cancel an article:
	   news:MESSAGE-ID?cancel

	 (Note that message IDs may contain / before the @:)

	   news:SOME/ID@HOST?headers=all
	   news:/SOME/ID@HOST?headers=all
	   news:/SOME?ID@HOST?headers=all
        are the same as
	   news://HOST/SOME/ID@HOST?headers=all
	   news://HOST//SOME/ID@HOST?headers=all
	   news://HOST//SOME?ID@HOST?headers=all
        bug: if the ID is <//xxx@host> we will parse this correctly:
          news://non-default-host///xxx@host
        but will mis-parse it if it's on the default host:
          news://xxx@host
        whoever had the idea to leave the <> off the IDs should be flogged.
		So, we'll make sure we quote / in message IDs as %2F.

 */
nsresult nsNNTPProtocol::ParseURL(nsIURL * aURL, char ** aHostAndPort, PRBool * bValP, char ** aGroup, char ** aMessageID,
								  char ** aCommandSpecificData)
{
	char * hostAndPort = NULL;
	PRInt32 status = 0;
	PRInt32 port = 0;
	char *group = 0;
    char *message_id = 0;
    char *command_specific_data = 0;
    const char *path_part;
	const char * url = 0;
	char * s = 0;
	const char * host;
	
	aURL->GetHost(&host);
	if (host)
		hostAndPort = nsCRT::strdup(host);

	// mscott: I took out default code to generate host and port
    // if the url didn't have any...add this later...
	if (!hostAndPort)
	{
	   status = MK_NO_NEWS_SERVER;
	   goto FAIL;
	}


	/* If a port was specified, but it was the default port, pretend
	   it wasn't specified.
	*/
  	s = PL_strchr (hostAndPort, ':');
	if (s && PR_sscanf(s+1, " %u ", &port) == 1 && HG05998)
		*s = 0;

	// I think the path part is just the file part of the nsIURL interface...
//	aURL->GetFile(&path_part); 
	aURL->GetSpec(&url);
	path_part = PL_strchr (url, ':');
    PR_ASSERT (path_part);
    if (!path_part)
	{
	  status = -1;
	  goto FAIL;
	}

	path_part++;
	if (path_part[0] == '/' && path_part[1] == '/')
	{
	  /* Skip over host name. */
	  path_part = PL_strchr (path_part + 2, '/');
	  if (path_part)
		path_part++;
	}
  
	if (!path_part)
		path_part = "";

	group = nsCRT::strdup (path_part);
	if (!group)
	{
	  status = MK_OUT_OF_MEMORY;
	  goto FAIL;
	}

	nsUnescape (group);

    /* "group" now holds the part after the host name:
	 "message@id?search" or "/group/xxx?search" or "/message?id@xx?search"

	 If there is an @, this is a message ID; else it is a group.
	 Either way, there may be search data at the end.
    */

	s = PL_strchr (group, '@');
    if (s)
	{
	  message_id = group;
	  group = 0;
	}
    else if (!*group)
	{
	  PR_Free (group);
	  group = 0;
	}

    /* At this point, the search data is attached to `message_id' (if there
	 is one) or `group' (if there is one) or `host_and_port' (otherwise.)
	 Seperate the search data from what it is clinging to, being careful
	 to interpret the "?" only if it comes after the "@" in an ID, since
	 the syntax of message IDs is tricky.  (There may be a ? in the
	 random-characters part of the ID (before @), but not in the host part
	 (after @).)
   */
   if (message_id || group || hostAndPort)
   {
	  char *start;
	  if (message_id)
	  {
		  /* Move past the @. */
		  PR_ASSERT (s && *s == '@');
		  start = s;
	  }
	  else
	  {
		  start = group ? group : hostAndPort;
	  }

	  /* Take off the "?" or "#" search data */
	  for (s = start; *s; s++)
		if (*s == '?' || *s == '#')
		  break;
	  
	  if (*s)
	  {
		  command_specific_data = nsCRT::strdup (s);
		  *s = 0;
		  if (!command_specific_data)
			{
			  status = MK_OUT_OF_MEMORY;
			  goto FAIL;
			}
		}

	  /* Discard any now-empty strings. */
	  if (message_id && !*message_id)
		{
		  PR_Free (message_id);
		  message_id = 0;
		}
	  else if (group && !*group)
		{
		  PR_Free (group);
		  group = 0;
		}
	}

  FAIL:
  PR_ASSERT (!message_id || message_id != group);
  if (status >= 0)
  {
      if (aHostAndPort)
          *aHostAndPort = hostAndPort;
      
      if (aGroup) *aGroup = group;
      else PR_FREEIF(group);
      
      if (aMessageID) *aMessageID = message_id;
      else PR_FREEIF(message_id);
      
	  if (aCommandSpecificData) *aCommandSpecificData = command_specific_data;
      else PR_FREEIF(command_specific_data);
      
	  HG45873
  }
  else
  {
	  PR_FREEIF (group);
	  PR_FREEIF (message_id);
	  PR_FREEIF (command_specific_data);
  }

  // mscott - this function might need to be re-written to use nsresults
  // so we don't lose the nature of the error in this return code I'm adding.
  if (status < 0)
	  return NS_ERROR_FAILURE;
  else
	  return NS_OK;
}
/*
 * Writes the data contained in dataBuffer into the current output stream. It also informs
 * the transport layer that this data is now available for transmission.
 * Returns a positive number for success, 0 for failure (not all the bytes were written to the
 * stream, etc). We need to make another pass through this file to install an error system (mscott)
 */

PRInt32 nsNNTPProtocol::SendData(nsIURL * aURL, const char * dataBuffer)
{
	NNTP_LOG_WRITE(dataBuffer);
	return nsMsgProtocol::SendData(aURL, dataBuffer); // base class actually transmits the data
}

/* gets the response code from the nntp server and the
 * response line
 *
 * returns the TCP return code from the read
 */
PRInt32 nsNNTPProtocol::NewsResponse(nsIInputStream * inputStream, PRUint32 length)
{
	char * line = nsnull;
	PRUint32 status = 0;

	NS_PRECONDITION(nsnull != inputStream, "invalid input stream");
	
	PRBool pauseForMoreData = PR_FALSE;
	line = m_lineStreamBuffer->ReadNextLine(inputStream, status, pauseForMoreData);

#ifdef DEBUG_NEWS
	printf("read: %s\n",line);
#endif

    if(pauseForMoreData)
	{
		SetFlag(NNTP_PAUSE_FOR_READ);
		return 0;
	}

    /* if TCP error of if there is not a full line yet return */
    if(status < 0)
	{
        m_runningURL->SetErrorMessage(NET_ExplainErrorDetails(MK_TCP_READ_ERROR, PR_GetOSError()));
        /* return TCP error
         */
        return MK_TCP_READ_ERROR;
	}
	else if(!line)
         return status;

    ClearFlag(NNTP_PAUSE_FOR_READ);  /* don't pause if we got a line */
	HG43574
    /* almost correct */
    if(status > 1)
	{
#ifdef DEBUG_NEWS
	printf("received %d bytes\n", status);
#endif
#ifdef UNREADY_CODE
        ce->bytes_received += status;
        FE_GraphProgress(ce->window_id, ce->URL_s, ce->bytes_received, status, ce->URL_s->content_length);
#endif
	}

    NET_SACopy(&m_responseText, line+4);

	m_previousResponseCode = m_responseCode;

    PR_sscanf(line, "%d", &m_responseCode);

	/* authentication required can come at any time
	 */
#ifdef CACHE_NEWSGRP_PASSWORD
	/*
	 * This is an effort of trying to cache the username/password
	 * per newsgroup. It is extremely hard to make it work along with
	 * nntp voluntary password checking mechanism. We are backing this 
	 * feature out. Instead of touching various of backend msg files
	 * at this late Dogbert 4.0 beta4 game, the infrastructure will
	 * remain in the msg library. We only modify codes within this file.
	 * Maybe one day we will try to do it again. Zzzzz -- jht
	 */

	if(MK_NNTP_RESPONSE_AUTHINFO_REQUIRE == m_responseCode ||
       MK_NTTP_RESPONSE_AUTHINFO_SIMPLE_REQUIRE == m_responseCode || 
	   MK_NNTP_RESPONSE_PERMISSION_DENIED == m_responseCode)
	  {
        m_nextState = NNTP_BEGIN_AUTHORIZE;
		if (MK_NNTP_RESPONSE_PERMISSION_DENIED == m_responseCode) 
		{
			if (MK_NNTP_RESPONSE_TYPE_OK == MK_NNTP_RESPONSE_TYPE(m_previousResponseCode) 
			{
				if (net_news_last_username_probably_valid)
				  net_news_last_username_probably_valid = PR_FALSE;
				else 
				{
                  m_newsgroup->SetUsername(NULL);
                  m_newsgroup->SetPassword(NULL);
				}
			}
			else 
			{
			  net_news_last_username_probably_valid = PR_FALSE;
			  if (NNTP_PASSWORD_RESPONSE == m_nextStateAfterResponse) 
			  {
                  m_newsgroup->SetUsername(NULL);
                  m_newsgroup->SetPassword(NULL);
			  }
			}
		}
	  }
#else
	if (MK_NNTP_RESPONSE_AUTHINFO_REQUIRE == m_responseCode ||
        MK_NNTP_RESPONSE_AUTHINFO_SIMPLE_REQUIRE == m_responseCode) 
	{
		m_nextState = NNTP_BEGIN_AUTHORIZE;
	}
	else if (MK_NNTP_RESPONSE_PERMISSION_DENIED == m_responseCode)
	{
	    net_news_last_username_probably_valid = PR_FALSE;
#ifdef UNREADY_CODE
		return net_display_html_error_state(ce);
#else
		PR_FREEIF(line);
		return (0);
#endif
	}
#endif
	else
    	m_nextState = m_nextStateAfterResponse;

	PR_FREEIF(line);
    return(0);  /* everything ok */
}

HG43072

/* interpret the server response after the connect
 *
 * returns negative if the server responds unexpectedly
 */
 
PRInt32 nsNNTPProtocol::LoginResponse()
{
	PRBool postingAllowed = m_responseCode == MK_NNTP_RESPONSE_POSTING_ALLOWED;

    if(MK_NNTP_RESPONSE_TYPE(m_responseCode)!=MK_NNTP_RESPONSE_TYPE_OK)
	{
		m_runningURL->SetErrorMessage(NET_ExplainErrorDetails(MK_NNTP_ERROR_MESSAGE, m_responseText));

    	m_nextState = NNTP_ERROR;
#ifdef UNREADY_CODE
        cd->control_con->prev_cache = PR_FALSE; /* to keep if from reconnecting */
#endif
        return MK_BAD_NNTP_CONNECTION;
	}

	//  ###mscott: I'm commenting this out right now since phil has a comment saying it is dead code...
	//	cd->control_con->posting_allowed = postingAllowed; /* ###phil dead code */
    
	m_newsHost->SetPostingAllowed(postingAllowed);
    m_nextState = NNTP_SEND_MODE_READER;
    return(0);  /* good */
}

PRInt32 nsNNTPProtocol::SendModeReader()
{  
	nsresult status = SendData(m_runningURL, NNTP_CMD_MODE_READER); 
    m_nextState = NNTP_RESPONSE;
    m_nextStateAfterResponse = NNTP_SEND_MODE_READER_RESPONSE;
    SetFlag(NNTP_PAUSE_FOR_READ); 
    return(status);
}

PRInt32 nsNNTPProtocol::SendModeReaderResponse()
{
	SetFlag(NNTP_READER_PERFORMED);
	/* ignore the response code and continue
	 */
    PRBool pushAuth;
    nsresult rv = m_newsHost->GetPushAuth(&pushAuth);
    if (NS_SUCCEEDED(rv) && pushAuth)
		/* if the news host is set up to require volunteered (pushed) authentication,
		 * do that before we do anything else
		 */
		m_nextState = NNTP_BEGIN_AUTHORIZE;
	else
		m_nextState = SEND_LIST_EXTENSIONS;

	return(0);
}

PRInt32 nsNNTPProtocol::SendListExtensions()
{
	PRInt32 status = SendData(m_runningURL, NNTP_CMD_LIST_EXTENSIONS);

	m_nextState = NNTP_RESPONSE;
	m_nextStateAfterResponse = SEND_LIST_EXTENSIONS_RESPONSE;
	ClearFlag(NNTP_PAUSE_FOR_READ);
	return status;
}

PRInt32 nsNNTPProtocol::SendListExtensionsResponse(nsIInputStream * inputStream, PRUint32 length)
{
	PRUint32 status = 0; 

	if (MK_NNTP_RESPONSE_TYPE(m_responseCode) == MK_NNTP_RESPONSE_TYPE_OK)
	{
		char *line = nsnull;

		PRBool pauseForMoreData = PR_FALSE;
		line = m_lineStreamBuffer->ReadNextLine(inputStream, status, pauseForMoreData);

		if(pauseForMoreData)
		{
			SetFlag(NNTP_PAUSE_FOR_READ);
			return 0;
		}
		if (!line)
			return status;  /* no line yet */
		if (status < 0)
		{
			m_runningURL->SetErrorMessage(NET_ExplainErrorDetails(MK_TCP_READ_ERROR, PR_GetOSError()));

			/* return TCP error */
			return MK_TCP_READ_ERROR;
		}

		if ('.' != line[0])
            m_newsHost->AddExtension(line);
		else
		{
			/* tell libmsg that it's ok to ask this news host for extensions */		
			m_newsHost->SetSupportsExtensions(PR_TRUE);
			/* all extensions received */
			m_nextState = SEND_LIST_SEARCHES;
			ClearFlag(NNTP_PAUSE_FOR_READ);
		}
	}
	else
	{
		/* LIST EXTENSIONS not recognized 
		 * tell libmsg not to ask for any more extensions and move on to
		 * the real NNTP command we were trying to do. */
		 
		 m_newsHost->SetSupportsExtensions(PR_FALSE);
		 m_nextState = SEND_FIRST_NNTP_COMMAND;
	}

	return status;
}

PRInt32 nsNNTPProtocol::SendListSearches()
{  
    nsresult rv;
    PRBool searchable=PR_FALSE;
	PRInt32 status = 0;
    
    rv = m_newsHost->QueryExtension("SEARCH",&searchable);
    if (NS_SUCCEEDED(rv) && searchable)
	{
		status = SendData(m_runningURL, NNTP_CMD_LIST_SEARCHES);

		m_nextState = NNTP_RESPONSE;
		m_nextStateAfterResponse = SEND_LIST_SEARCHES_RESPONSE;
		SetFlag(NNTP_PAUSE_FOR_READ);
	}
	else
	{
		/* since SEARCH isn't supported, move on to GET */
		m_nextState = NNTP_GET_PROPERTIES;
		ClearFlag(NNTP_PAUSE_FOR_READ);
	}

	return status;
}

PRInt32 nsNNTPProtocol::SendListSearchesResponse(nsIInputStream * inputStream, PRUint32 length)
{
	char *line = nsnull;
	PRUint32 status = 0;

	NS_PRECONDITION(inputStream, "invalid input stream");
	
	PRBool pauseForMoreData = PR_FALSE;
	line = m_lineStreamBuffer->ReadNextLine(inputStream, status, pauseForMoreData);

	NNTP_LOG_READ(line);

	if(pauseForMoreData)
	{
		SetFlag(NNTP_PAUSE_FOR_READ);
		return 0;
	}
	if (!line)
		return status;  /* no line yet */
	if (status < 0)
	{
		m_runningURL->SetErrorMessage(NET_ExplainErrorDetails(MK_TCP_READ_ERROR, PR_GetOSError()));
		/* return TCP error */
		return MK_TCP_READ_ERROR;
	}

	if ('.' != line[0])
	{
		m_newsHost->AddSearchableGroup(line);
	}
	else
	{
		/* all searchable groups received */
		/* LIST SRCHFIELDS is legal if the server supports the SEARCH extension, which */
		/* we already know it does */
		m_nextState = NNTP_LIST_SEARCH_HEADERS;
		ClearFlag(NNTP_PAUSE_FOR_READ); 
	}

	PR_FREEIF(line);
	return status;
}

PRInt32 nsNNTPProtocol::SendListSearchHeaders()
{
	PRInt32 status = SendData(m_runningURL, NNTP_CMD_LIST_SEARCH_FIELDS);

	m_nextState = NNTP_RESPONSE;
	m_nextStateAfterResponse = NNTP_LIST_SEARCH_HEADERS_RESPONSE;
	SetFlag(NNTP_PAUSE_FOR_READ);

	return status;
}

PRInt32 nsNNTPProtocol::SendListSearchHeadersResponse(nsIInputStream * inputStream, PRUint32 length)
{
	char *line = nsnull;
	PRUint32 status = 0; 

	PRBool pauseForMoreData = PR_FALSE;
	line = m_lineStreamBuffer->ReadNextLine(inputStream, status, pauseForMoreData);

	if(pauseForMoreData)
	{
		SetFlag(NNTP_PAUSE_FOR_READ);
		return 0;
	}
	if (!line)
		return status;  /* no line yet */
	if (status < 0)
	{
		m_runningURL->SetErrorMessage(NET_ExplainErrorDetails(MK_TCP_READ_ERROR, PR_GetOSError()));

		/* return TCP error */
		return MK_TCP_READ_ERROR;
	}

	if ('.' != line[0])
        m_newsHost->AddSearchableHeader(line);
	else
	{
		m_nextState = NNTP_GET_PROPERTIES;
		ClearFlag(NNTP_PAUSE_FOR_READ);
	}

	PR_FREEIF(line);
	return status;
}

PRInt32 nsNNTPProtocol::GetProperties()
{
    nsresult rv;
    PRBool setget=PR_FALSE;
	PRInt32 status = 0; 
    
    rv = m_newsHost->QueryExtension("SETGET",&setget);
    if (NS_SUCCEEDED(rv) && setget)
	{
		status = SendData(m_runningURL, NNTP_CMD_GET_PROPERTIES);
		m_nextState = NNTP_RESPONSE;
		m_nextStateAfterResponse = NNTP_GET_PROPERTIES_RESPONSE;
		SetFlag(NNTP_PAUSE_FOR_READ);
	}
	else
	{
		/* since GET isn't supported, move on LIST SUBSCRIPTIONS */
		m_nextState = SEND_LIST_SUBSCRIPTIONS;
		ClearFlag(NNTP_PAUSE_FOR_READ);
	}
	return status;
}

PRInt32 nsNNTPProtocol::GetPropertiesResponse(nsIInputStream * inputStream, PRUint32 length)
{
	char *line = NULL;
	PRUint32 status = 0;

	PRBool pauseForMoreData = PR_FALSE;
	line = m_lineStreamBuffer->ReadNextLine(inputStream, status, pauseForMoreData);

	if(pauseForMoreData)
	{
		SetFlag(NNTP_PAUSE_FOR_READ);
		return 0;
	}
	if (!line)
		return status;  /* no line yet */
	if (status < 0)
	{
		m_runningURL->SetErrorMessage(NET_ExplainErrorDetails(MK_TCP_READ_ERROR, PR_GetOSError()));

		/* return TCP error */
		return MK_TCP_READ_ERROR;
	}

	if ('.' != line[0])
	{
		char *propertyName = nsCRT::strdup(line);
		if (propertyName)
		{
			char *space = PL_strchr(propertyName, ' ');
			if (space)
			{
				char *propertyValue = space + 1;
				*space = '\0';
                m_newsHost->AddPropertyForGet(propertyName, propertyValue);
			}
			PR_Free(propertyName);
		}
	}
	else
	{
		/* all GET properties received, move on to LIST SUBSCRIPTIONS */
		m_nextState = SEND_LIST_SUBSCRIPTIONS;
		ClearFlag(NNTP_PAUSE_FOR_READ);
	}

	PR_FREEIF(line);
	return status;
}

PRInt32 nsNNTPProtocol::SendListSubscriptions()
{
   PRInt32 status = 0; 
#if 0
    nsresult rv;
    PRBool searchable=PR_FALSE;
    rv = m_newsHost->QueryExtension("LISTSUBSCR",&listsubscr);
    if (NS_SUCCEEDED(rv) && listsubscr)
#else
	if (0)
#endif
	{
		status = SendData(m_runningURL, NNTP_CMD_LIST_SUBSCRIPTIONS);
		m_nextState = NNTP_RESPONSE;
		m_nextStateAfterResponse = SEND_LIST_SUBSCRIPTIONS_RESPONSE;
		SetFlag(NNTP_PAUSE_FOR_READ);
	}
	else
	{
		/* since LIST SUBSCRIPTIONS isn't supported, move on to real work */
		m_nextState = SEND_FIRST_NNTP_COMMAND;
		ClearFlag(NNTP_PAUSE_FOR_READ);
	}

	return status;
}

PRInt32 nsNNTPProtocol::SendListSubscriptionsResponse(nsIInputStream * inputStream, PRUint32 length)
{
	char *line = NULL;
	PRUint32 status = 0;

	PRBool pauseForMoreData = PR_FALSE;
	line = m_lineStreamBuffer->ReadNextLine(inputStream, status, pauseForMoreData);

	if(pauseForMoreData)
	{
		SetFlag(NNTP_PAUSE_FOR_READ);
		return 0;
	}
	if (!line)
		return status;  /* no line yet */
	if (status < 0)
	{
		m_runningURL->SetErrorMessage(NET_ExplainErrorDetails(MK_TCP_READ_ERROR, PR_GetOSError()));
		/* return TCP error */
		return MK_TCP_READ_ERROR;
	}

	if ('.' != line[0])
	{
#if 0
		char *urlScheme;
		HG56946
		char *url = PR_smprintf ("%s//%s/%s", urlScheme, m_hostName, line);
		if (url)
			MSG_AddSubscribedNewsgroup (cd->pane, url);
#endif
	}
	else
	{
		/* all default subscriptions received */
		m_nextState = SEND_FIRST_NNTP_COMMAND;
		ClearFlag(NNTP_PAUSE_FOR_READ);
	}

	PR_FREEIF(line);
	return status;
}

/* figure out what the first command is and send it
 *
 * returns the status from the NETWrite */

PRInt32 nsNNTPProtocol::SendFirstNNTPCommand(nsIURL * url)
{
	char *command=0;
	PRInt32 status = 0;

	if (m_typeWanted == ARTICLE_WANTED)
	  {
		const char *group = 0;
		PRUint32 number = 0;

		nsresult rv;
        nsCOMPtr <nsINNTPNewsgroup> newsgroup;
		if (!m_newsHost) {
#ifdef DEBUG_NEWS
			printf("m_newsHost is null, panic!\n");
#endif
			return -1;
		}
        rv = m_newsHost->GetNewsgroupAndNumberOfID(m_path,
                                                   getter_AddRefs(newsgroup),
                                                   &number);
		if (NS_SUCCEEDED(rv) && newsgroup && number)
		  {
			m_articleNumber = number;
            m_newsgroup = newsgroup;

			if (m_currentGroup && !PL_strcmp (m_currentGroup, group))
			  m_nextState = NNTP_SEND_ARTICLE_NUMBER;
			else
			  m_nextState = NNTP_SEND_GROUP_FOR_ARTICLE;

			ClearFlag(NNTP_PAUSE_FOR_READ);
			return 0;
		  }
	  }

	if(m_typeWanted == NEWS_POST)
      {  /* posting to the news group */
        NET_SACopy(&command, "POST");
      }
    else if(m_typeWanted == READ_NEWS_RC)
      {
		/* extract post method from the url when we have it... */
#ifdef HAVE_NEWS_URL
		if(ce->URL_s->method == URL_POST_METHOD ||
								PL_strchr(ce->URL_s->address, '?'))
        	m_nextState = NEWS_NEWS_RC_POST;
		else
#endif
        	m_nextState = NEWS_DISPLAY_NEWS_RC;
		return(0);
      } 
	else if(m_typeWanted == NEW_GROUPS)
	{
        PRUint32 last_update;
        nsresult rv;
		
		if (!m_newsHost) {
#ifdef DEBUG_NEWS
			printf("m_newsHost is null, panic!\n");
#endif
			return -1;
		}
        rv = m_newsHost->GetLastUpdatedTime(&last_update);
		char small_buf[64];
        PRExplodedTime  expandedTime;

		if(!last_update)
		{	
			m_runningURL->SetErrorMessage(NET_ExplainErrorDetails(MK_NNTP_NEWSGROUP_SCAN_ERROR));
			m_nextState = NEWS_ERROR;
			return(MK_INTERRUPTED);
		}
	
		/* subtract some hours just to be sure */
		last_update -= NEWGROUPS_TIME_OFFSET;
	
        {
           PRInt64  secToUSec, timeInSec, timeInUSec;
           LL_I2L(timeInSec, last_update);
           LL_I2L(secToUSec, PR_USEC_PER_SEC);
           LL_MUL(timeInUSec, timeInSec, secToUSec);
           PR_ExplodeTime(timeInUSec, PR_LocalTimeParameters, &expandedTime);
        }
		PR_FormatTimeUSEnglish(small_buf, sizeof(small_buf), 
                               "NEWGROUPS %y%m%d %H%M%S", &expandedTime);
		
        NET_SACopy(&command, small_buf);

	}
    else if(m_typeWanted == LIST_WANTED)
    {
	
		ClearFlag(NNTP_USE_FANCY_NEWSGROUP);
        PRUint32 last_update;
      	
		if (!m_newsHost) {
#ifdef DEBUG
			printf("m_newsHost is null, panic!\n");
#endif
			return -1;
		}
		nsresult rv = m_newsHost->GetLastUpdatedTime(&last_update);
        
        if (NS_SUCCEEDED(rv) && last_update!=0)
		{
			m_nextState = DISPLAY_NEWSGROUPS;
        	return(0);
	    }
		else
		{
#ifdef UNREADY_CODE
#ifdef BUG_21013
			if(!FE_Confirm(ce->window_id, XP_GetString(XP_CONFIRM_SAVE_NEWSGROUPS)))
	  		  {
				m_nextState = NEWS_ERROR;
				return(MK_INTERRUPTED);
	  		  }
#endif /* BUG_21013 */
#endif
			PRBool xactive=PR_FALSE;
			rv = m_newsHost->QueryExtension("XACTIVE",&xactive);
			if (NS_SUCCEEDED(rv) && xactive)
			{
				NET_SACopy(&command, "LIST XACTIVE");
				SetFlag(NNTP_USE_FANCY_NEWSGROUP);
			}
			else
			{
				NET_SACopy(&command, "LIST");
			}
		}
	}
	else if(m_typeWanted == GROUP_WANTED) 
    {
        /* Don't use MKParse because the news: access URL doesn't follow traditional
         * rules. For instance, if the article reference contains a '#',
         * the rest of it is lost.
         */
        char * slash;
        char * group_name;
        nsresult rv=NS_ERROR_NULL_POINTER;

        NET_SACopy(&command, "GROUP ");
        if (!m_newsgroup) {
#ifdef DEBUG_NEWS
			printf("m_newsgroup is null, panic!\n");
#endif
			return -1;
		}
            
		rv = m_newsgroup->GetName(&group_name);
        slash = PL_strchr(group_name, '/');
		
        m_firstArticle = 0;
        m_lastArticle = 0;
        if (slash)
		{
            *slash = '\0';
            (void) PR_sscanf(slash+1, "%d-%d", &m_firstArticle, &m_lastArticle);
		}

        NET_SACopy (&m_currentGroup, group_name);
        NET_SACat(&command, m_currentGroup);
      }
	else if (m_typeWanted == SEARCH_WANTED)
	{
		nsresult rv;
		PRBool searchable=PR_FALSE;
		if (!m_newsHost) {
#ifdef DEBUG
			printf("m_newsHost is null, panic!\n");
#endif
			return -1;
		}
		rv = m_newsHost->QueryExtension("SEARCH", &searchable);
		if (NS_SUCCEEDED(rv) && searchable)
		{
			/* use the SEARCH extension */
			char *slash = PL_strchr (m_commandSpecificData, '/');
			if (slash)
			{
				char *allocatedCommand = MSG_UnEscapeSearchUrl (slash + 1);
				if (allocatedCommand)
				{
					NET_SACopy (&command, allocatedCommand);
					PR_Free(allocatedCommand);
				}
			}
			m_nextState = NNTP_RESPONSE;
			m_nextStateAfterResponse = NNTP_SEARCH_RESPONSE;
		}
		else
		{
            char *group_name;
            
			/* for XPAT, we have to GROUP into the group before searching */
			NET_SACopy(&command, "GROUP ");
			if (!m_newsgroup) {
#ifdef DEBUG_NEWS
				printf("m_newsgroup is null, panic!\n");
#endif
				return -1;
			}
            rv = m_newsgroup->GetName(&group_name);
            NET_SACat (&command, group_name);
			m_nextState = NNTP_RESPONSE;
			m_nextStateAfterResponse = NNTP_XPAT_SEND;
		}
	}
	else if (m_typeWanted == PRETTY_NAMES_WANTED)
	{
		nsresult rv;
		PRBool listpretty=PR_FALSE;
		rv = m_newsHost->QueryExtension("LISTPRETTY",&listpretty);
		if (NS_SUCCEEDED(rv) && listpretty)
		{
			m_nextState = NNTP_LIST_PRETTY_NAMES;
			return 0;
		}
		else
		{
			PR_ASSERT(PR_FALSE);
			m_nextState = NNTP_ERROR;
		}
	}
	else if (m_typeWanted == PROFILE_WANTED)
	{
		char *slash = PL_strchr (m_commandSpecificData, '/');
		if (slash)
		{
			char *allocatedCommand = MSG_UnEscapeSearchUrl (slash + 1);
			if (allocatedCommand)
			{
				NET_SACopy(&command, allocatedCommand);
				PR_Free(allocatedCommand);
			}
		}
		m_nextState = NNTP_RESPONSE;
#ifdef UNREADY_CODE
		if (PL_strstr(ce->URL_s->address, "PROFILE NEW"))
			m_nextStateAfterResponse = NNTP_PROFILE_ADD_RESPONSE;
		else
#endif
			m_nextStateAfterResponse = NNTP_PROFILE_DELETE_RESPONSE;
	}
	else if (m_typeWanted == IDS_WANTED)
	{
		m_nextState = NNTP_LIST_GROUP;
		return 0;
	}
    else  /* article or cancel */
	{
		if (m_typeWanted == CANCEL_WANTED)
			NET_SACopy(&command, "HEAD ");
		else
			NET_SACopy(&command, "ARTICLE ");
		if (*m_path != '<')
			NET_SACat(&command,"<");
		NET_SACat(&command, m_path);
		if (PL_strchr(command+8, '>')==0) 
			NET_SACat(&command,">");
	}

    NET_SACat(&command, CRLF);
	status = SendData(m_runningURL, command);
    PR_Free(command);

	m_nextState = NNTP_RESPONSE;
	if (m_typeWanted != SEARCH_WANTED && m_typeWanted != PROFILE_WANTED)
		m_nextStateAfterResponse = SEND_FIRST_NNTP_COMMAND_RESPONSE;
	SetFlag(NNTP_PAUSE_FOR_READ);
    return(status);

} /* sent first command */


/* interprets the server response from the first command sent
 *
 * returns negative if the server responds unexpectedly 
 */

PRInt32 nsNNTPProtocol::SendFirstNNTPCommandResponse()
{
	PRInt32 status = 0;
	PRInt32 major_opcode = MK_NNTP_RESPONSE_TYPE(m_responseCode);

	if((major_opcode == MK_NNTP_RESPONSE_TYPE_CONT &&
        m_typeWanted == NEWS_POST)
	 	|| (major_opcode == MK_NNTP_RESPONSE_TYPE_OK &&
            m_typeWanted != NEWS_POST) )
      {

        m_nextState = SETUP_NEWS_STREAM;
		SetFlag(NNTP_SOME_PROTOCOL_SUCCEEDED);
        return(0);  /* good */
      }
    else
      {
		if (m_responseCode == MK_NNTP_RESPONSE_GROUP_NO_GROUP &&
            m_typeWanted == GROUP_WANTED) {
#ifdef DEBUG_NEWS
            printf("group not found!\n");            
#endif
#ifdef UNREADY_CODE
        MSG_GroupNotFound(cd->pane, cd->host, cd->control_con->current_group, TRUE /* opening group */);
#else
            m_newsHost->GroupNotFound(m_currentGroup,
                                          PR_TRUE /* opening */);
#endif
        }
#ifdef UNREADY_CODE
		return net_display_html_error_state(ce);
#else
        /* if the server returned a 400 error then it is an expected
         * error.  the NEWS_ERROR state will not sever the connection
         */
        if(major_opcode == 4)
          m_nextState = NEWS_ERROR;
        else
          m_nextState = NNTP_ERROR;

        /* START OF HACK TO DISPLAY THE ERROR IN THE MESSAGE PANE */
        nsresult rv = NS_OK;
        char *group_name = nsnull;
        char outputBuffer[OUTPUT_BUFFER_SIZE];

        m_tempErrorFile.Delete(PR_FALSE);
        nsISupports * supports;
        NS_NewIOFileStream(&supports, m_tempErrorFile, PR_WRONLY | PR_CREATE_FILE | PR_TRUNCATE, 00700);
        m_tempErrorStream = do_QueryInterface(supports);
        NS_IF_RELEASE(supports);
        
        if (m_newsgroup) {
            rv = m_newsgroup->GetName(&group_name);
        }

        if (NS_SUCCEEDED(rv) && group_name && m_tempErrorStream) {
            PRUint32 count = 0;
            
            PR_snprintf(outputBuffer,OUTPUT_BUFFER_SIZE, UNTIL_STRING_BUNDLES_XP_HTML_NEWS_ERROR, m_responseText);
        	m_tempErrorStream->Write(outputBuffer, PL_strlen(outputBuffer), &count);
            
            PR_snprintf(outputBuffer,OUTPUT_BUFFER_SIZE, UNTIL_STRING_BUNDLES_XP_HTML_ARTICLE_EXPIRED);
            m_tempErrorStream->Write(outputBuffer, PL_strlen(outputBuffer), &count);
            
	    nsMsgKey key = nsMsgKey_None;
	    rv = m_runningURL->GetMessageKey(&key);
            PR_ASSERT(m_messageID && (key != nsMsgKey_None));
			if (m_messageID && (key != nsMsgKey_None)) {
				PR_snprintf(outputBuffer,OUTPUT_BUFFER_SIZE,"<P>&lt;%.512s&gt; (%lu)", m_messageID, key);
				m_tempErrorStream->Write(outputBuffer, PL_strlen(outputBuffer), &count);
			}
            
            PR_snprintf(outputBuffer,OUTPUT_BUFFER_SIZE,"<P> <A HREF=\"%s/%s/%s?list-ids\">%s</A> </P>\n", kNewsRootURI, m_hostName, group_name, UNTIL_STRING_BUNDLES_XP_LIST_IDS_URL_TEXT);
            m_tempErrorStream->Write(outputBuffer, PL_strlen(outputBuffer), &count);
        }
  
        // and close the article file if it was open....
		if (m_tempErrorStream)
			m_tempErrorStream->Close();

        /* cut and paste from below */
        if (m_displayConsumer)
		{
			nsFilePath filePath(ERROR_PATH);
			nsFileURL  fileURL(filePath);
			char * error_path_url = PL_strdup(fileURL.GetAsString());

#ifdef DEBUG_NEWS
			printf("load this url to display the error message: %s\n", error_path_url);
#endif

			m_displayConsumer->LoadURL(nsAutoString(error_path_url).GetUnicode(), nsnull, PR_TRUE, nsURLReload, 0);
			
			PR_FREEIF(error_path_url);
		}
        /* cut and paste from below */
        /* END OF HACK */        
		return MK_NNTP_SERVER_ERROR;
#endif
      }

	/* start the graph progress indicator
     */
#ifdef UNREADY_CODE
    FE_GraphProgressInit(ce->window_id, ce->URL_s, ce->URL_s->content_length);
#endif
	SetFlag(NNTP_DESTROY_PROGRESS_GRAPH);
#ifdef UNREADY_CODE
    m_originalContentLength = ce->URL_s->content_length;
#endif
	return(status);
}

PRInt32 nsNNTPProtocol::SendGroupForArticle()
{
  nsresult rv;
  PRInt32 status = 0; 

  PR_FREEIF(m_currentGroup);
  rv = m_newsgroup->GetName(&m_currentGroup);
  PR_ASSERT(NS_SUCCEEDED(rv));
  char outputBuffer[OUTPUT_BUFFER_SIZE];
  
  PR_snprintf(outputBuffer, 
			OUTPUT_BUFFER_SIZE, 
			"GROUP %.512s" CRLF, 
			m_currentGroup);

  status = SendData(m_runningURL, outputBuffer);

  m_nextState = NNTP_RESPONSE;
  m_nextStateAfterResponse = NNTP_SEND_GROUP_FOR_ARTICLE_RESPONSE;
  SetFlag(NNTP_PAUSE_FOR_READ);

  return(status);
}

PRInt32 nsNNTPProtocol::SendGroupForArticleResponse()
{
  /* ignore the response code and continue
   */
  m_nextState = NNTP_SEND_ARTICLE_NUMBER;

  return(0);
}


PRInt32 nsNNTPProtocol::SendArticleNumber()
{
	char outputBuffer[OUTPUT_BUFFER_SIZE];
	PRInt32 status = 0; 
	PR_snprintf(outputBuffer, OUTPUT_BUFFER_SIZE, "ARTICLE %lu" CRLF, m_articleNumber);

	status = SendData(m_runningURL, outputBuffer);

    m_nextState = NNTP_RESPONSE;
    m_nextStateAfterResponse = SEND_FIRST_NNTP_COMMAND_RESPONSE;
    SetFlag(NNTP_PAUSE_FOR_READ);

    return(status);
}


PRInt32 nsNNTPProtocol::BeginArticle()
{
  if (m_typeWanted != ARTICLE_WANTED &&
	  m_typeWanted != CANCEL_WANTED)
	return 0;

  /*  Set up the HTML stream
   */ 
  PL_strfree (m_currentGroup);
#ifdef UNREADY_CODE
  ce->URL_s->content_type = nsCRT::strdup (MESSAGE_RFC822);
#endif

#ifdef NO_ARTICLE_CACHEING
  ce->format_out = CLEAR_CACHE_BIT (ce->format_out);
#endif

  if (m_typeWanted == CANCEL_WANTED)
  {
#ifdef UNREADY_CODE
	  PR_ASSERT(ce->format_out == FO_PRESENT);
	  ce->format_out = FO_PRESENT;
#endif
  }

  /* Only put stuff in the fe_data if this URL is going to get
	 passed to MIME_MessageConverter(), since that's the only
	 thing that knows what to do with this structure. */
#ifdef UNREADY_CODE
  if (CLEAR_CACHE_BIT(ce->format_out) == FO_PRESENT)
	{
	  status = net_InitializeNewsFeData (ce);
	  if (status < 0)
		{
		  /* #### what error message? */
		  return status;
		}
	}

  cd->stream = NET_StreamBuilder(ce->format_out, ce->URL_s, ce->window_id);
  PR_ASSERT (cd->stream);
  if (!cd->stream) return -1;
#endif

  // mscott: short term mime hack.....until libmime plays "nice" with a new stream converter
  // interface, we have to interact with it like we did in the old networking world...however this
  // would be hard to do now...in addition the code and effort would be wasted as we'd have to through
  // it away once libmime did use a new stream converter interface. So we are going to cheat and write
  // the article to file. We'll then call a load file url on our "temp" file. Then mkfile does all the work
  // with talking to the RFC-822->HTML stream converter....clever huh =).....

  // we are about to display an article so open up a temp file on the article...
  m_tempArticleFile.Delete(PR_FALSE);
  nsISupports * supports;
  NS_NewIOFileStream(&supports, m_tempArticleFile, PR_WRONLY | PR_CREATE_FILE | PR_TRUNCATE, 00700);
  m_tempArticleStream = do_QueryInterface(supports);
  NS_IF_RELEASE(supports);
  m_nextState = NNTP_READ_ARTICLE;

  return 0;
}

PRInt32 nsNNTPProtocol::ReadArticle(nsIInputStream * inputStream, PRUint32 length)
{
	char *line;
	PRUint32 status = 0;
	char outputBuffer[OUTPUT_BUFFER_SIZE];
	
	PRBool pauseForMoreData = PR_FALSE;
	line = m_lineStreamBuffer->ReadNextLine(inputStream, status, pauseForMoreData);
	if(pauseForMoreData)
	{
		SetFlag(NNTP_PAUSE_FOR_READ);
		return 0;
	}
	if(status > 1)
	{		
#ifdef DEBUG_NEWS
		printf("received %d bytes\n", status);
#endif
#ifdef UNREADY_CODE
		ce->bytes_received += status;
#endif
		//	FE_GraphProgress(ce->window_id, ce->URL_s,
		//					 ce->bytes_received, status,
		//					 ce->URL_s->content_length);
	}

	if(!line)
	  return(status);  /* no line yet or error */
	
	if (m_typeWanted == CANCEL_WANTED && m_responseCode != MK_NNTP_RESPONSE_ARTICLE_HEAD)
	{
		/* HEAD command failed. */
		PR_FREEIF(line);
		return MK_NNTP_CANCEL_ERROR;
	}

	if (line[0] == '.' && line[1] == 0)
	{
		if (m_typeWanted == CANCEL_WANTED)
			m_nextState = NEWS_START_CANCEL;
		else
			m_nextState = NEWS_DONE;

		// and close the article file if it was open....
		if (m_tempArticleStream)
			m_tempArticleStream->Close();

		if (m_displayConsumer)
		{
			nsFilePath filePath(ARTICLE_PATH);
			nsFileURL  fileURL(filePath);
			char * article_path_url = PL_strdup(fileURL.GetAsString());

#ifdef DEBUG_NEWS
			printf("load this url to display the message: %s\n", article_path_url);
#endif

			m_displayConsumer->LoadURL(nsAutoString(article_path_url).GetUnicode(), nsnull, PR_TRUE, nsURLReload, 0);
			
			PR_FREEIF(article_path_url);
		}

		// now mark the message as read
		printf("should we be marking later, after the message has finished loading?\n");
		nsCOMPtr<nsIMsgDBHdr> msgHdr;
		nsresult rv = NS_OK;

		rv = m_runningURL->GetMessageHeader(getter_AddRefs(msgHdr));

		if (NS_SUCCEEDED(rv)) {
			msgHdr->MarkRead(PR_TRUE);
		}

		ClearFlag(NNTP_PAUSE_FOR_READ);
	}
	else
	{
		if (line[0] == '.')
			PL_strcpy (outputBuffer, line + 1);
		else
			PL_strcpy (outputBuffer, line);

		/* When we're sending this line to a converter (ie,
		   it's a message/rfc822) use the local line termination
		   convention, not CRLF.  This makes text articles get
		   saved with the local line terminators.  Since SMTP
		   and NNTP mandate the use of CRLF, it is expected that
		   the local system will convert that to the local line
		   terminator as it is read.
		 */
		PL_strcat (outputBuffer, MSG_LINEBREAK);
		/* Don't send content-type to mime parser if we're doing a cancel
		  because it confuses mime parser into not parsing.
		  */
		if (m_typeWanted != CANCEL_WANTED || nsCRT::strncmp(outputBuffer, "Content-Type:", 13))
		{
			// for test purposes...we'd want to write this line out to an rfc-822 stream converter...
			// we don't have one now so print the data out so we can verify that we got it....
			printf("%s", outputBuffer);

            // if we are attempting to cancel, we want to snarf the headers and save the aside, which is what
            // ParseHeaderForCancel() does.
            if (m_typeWanted == CANCEL_WANTED) {
                ParseHeaderForCancel(outputBuffer);
            }
			if (m_tempArticleStream)
			{
				PRUint32 count = 0;
				m_tempArticleStream->Write(outputBuffer, PL_strlen(outputBuffer), &count);
			}
		}
	}

	PR_FREEIF(line);

	return 0;
}

void nsNNTPProtocol::ParseHeaderForCancel(char *buf)
{
    nsString header(buf, eOneByte);
    PRInt32 colon = header.Find(':');
    if (!colon)
		return;

    nsString value("", eOneByte);
    header.Right(value, header.Length() - colon -1);
    value.StripWhitespace();
      
    switch (header[0]) {
    case 'F': case 'f':
        if (header.Find("From") == 0) {
            if (m_cancelFromHdr) PR_FREEIF(m_cancelFromHdr);
			m_cancelFromHdr = PL_strdup(value.GetBuffer());
        }
        break;
    case 'M': case 'm':
        if (header.Find("Message-ID") == 0) {
            if (m_cancelID) PR_FREEIF(m_cancelID);
			m_cancelID = PL_strdup(value.GetBuffer());
        }
        break;
    case 'N': case 'n':
        if (header.Find("Newsgroups") == 0) {
            if (m_cancelNewsgroups) PR_FREEIF(m_cancelNewsgroups);
			m_cancelNewsgroups = PL_strdup(value.GetBuffer());
        }
        break;
     case 'D': case 'd':
        if (header.Find("Distributions") == 0) {
            if (m_cancelDistribution) PR_FREEIF(m_cancelDistribution);
			m_cancelDistribution = PL_strdup(value.GetBuffer());
        }       
        break;
    }

  return;
}

PRInt32 nsNNTPProtocol::BeginAuthorization()
{
	char * command = 0;
	char * username = 0;
	char * cp;
	PRInt32 status = 0;

#ifdef CACHE_NEWSGRP_PASSWORD
	/* reuse cached username from newsgroup folder info*/
	if (cd->pane && 
		(!net_news_last_username_probably_valid ||
		 (last_username_hostname && 
		  PL_strcasecmp(last_username_hostname, m_hostName)))) 
	{
      m_newsgroup->GetUsername(&username);
	  if (username && last_username &&
		  !PL_strcmp (username, last_username) &&
		  (m_previousResponseCode == MK_NNTP_RESPONSE_AUTHINFO_OK || 
		   m_previousResponseCode == MK_NNTP_RESPONSE_AUTHINFO_SIMPLE_OK ||
		   m_previousResponseCode == MK_NNTP_RESPONSE_GROUP_SELECTED)) {
		PR_FREEIF (username);
        m_newsgroup->SetUsername(NULL);
        m_newsgroup->SetPassword(NULL);
	  }
	}
#endif
	
	// mscott: right now we no longer have a pane...why do they want a pane here? 
	// commenting out for now....
#ifdef UNREADY_CODE
	if (cd->pane) 
#else
	if (1)
#endif
	{
	/* Following a snews://username:password@newhost.domain.com/newsgroup.topic
	 * backend calls MSG_Master::FindNewsHost() to locate the folderInfo and setting 
	 * the username/password to the newsgroup folderInfo
	 */
      m_newsgroup->GetUsername(&username);
	  if (username && *username)
	  {
		NET_SACopy(&last_username, username);
		NET_SACopy(&last_username_hostname, m_hostName);
		/* use it for only once */
        m_newsgroup->SetUsername(NULL);
	  }
	  else 
	  {
		  /* empty username; free and clear it so it will work with
		   * our logic
		   */
		  PR_FREEIF(username);
	  }
	}

	/* If the URL/m_hostName contains @ this must be triggered
	 * from the bookmark. Use the embed username if we could.
	 */
	if ((cp = PL_strchr(m_hostName, '@')) != NULL)
	  {
		/* in this case the username and possibly
		 * the password are in the URL
		 */
		char * colon;
		*cp = '\0';

		colon = PL_strchr(m_hostName, ':');
		if(colon)
			*colon = '\0';

		NET_SACopy(&username, m_hostName);
		NET_SACopy(&last_username, m_hostName);
		NET_SACopy(&last_username_hostname, cp+1);

		*cp = '@';

		if(colon)
			*colon = ':';
	  }
	/* reuse global saved username if we think it is
	 * valid
	 */
    if (!username && net_news_last_username_probably_valid)
	  {
		if( last_username_hostname &&
			!PL_strcasecmp(last_username_hostname, m_hostName) )
			NET_SACopy(&username, last_username);
		else
			net_news_last_username_probably_valid = PR_FALSE;
	  }


	if (!username) 
	{
#ifdef UNREADY_CODE
#if defined(CookiesAndSignons)
	  username = SI_Prompt(ce->window_id,
			       XP_GetString(XP_PROMPT_ENTER_USERNAME),
                               "",
			       m_hostName);

#else
	  username = FE_Prompt(ce->window_id,
						   XP_GetString(XP_PROMPT_ENTER_USERNAME),
						   username ? username : "");
#endif
	  
#endif // UNREADY_CODE

	  /* reset net_news_last_username_probably_valid to false */
	  net_news_last_username_probably_valid = PR_FALSE;
	  if(!username) 
	  {
		m_runningURL->SetErrorMessage(
		  NET_ExplainErrorDetails( MK_NNTP_AUTH_FAILED, "Aborted by user"));
		return(MK_NNTP_AUTH_FAILED);
	  }
	  else 
	  {
		NET_SACopy(&last_username, username);
		NET_SACopy(&last_username_hostname, m_hostName);
	  }
	} // !username

#ifdef CACHE_NEWSGRP_PASSWORD
    if (NS_SUCCEEDED(m_newsgroup->GetUsername(&username)) {
	  munged_username = HG64643 (username);
      m_newsgroup->SetUsername(munged_username);
	  PR_FreeIF(munged_username);
	}
#endif

	NET_SACopy(&command, "AUTHINFO user ");
	NET_SACopy(&command, username);
	NET_SACopy(&command, CRLF);

	status = SendData(m_runningURL, command);

	PR_Free(command);
	PR_Free(username);

	m_nextState = NNTP_RESPONSE;
	m_nextStateAfterResponse = NNTP_AUTHORIZE_RESPONSE;;

	SetFlag(NNTP_PAUSE_FOR_READ);

	return status;
}

PRInt32 nsNNTPProtocol::AuthorizationResponse()
{
	PRInt32 status = 0;

    if (MK_NNTP_RESPONSE_AUTHINFO_OK == m_responseCode ||
        MK_NNTP_RESPONSE_AUTHINFO_SIMPLE_OK == m_responseCode) 
	  {
		/* successful login */
        nsresult rv;
        PRBool pushAuth;
		/* If we're here because the host demanded authentication before we
		 * even sent a single command, then jump back to the beginning of everything
		 */
        rv = m_newsHost->GetPushAuth(&pushAuth);
        
        if (!TestFlag(NNTP_READER_PERFORMED))
			m_nextState = NNTP_SEND_MODE_READER;
		/* If we're here because the host needs pushed authentication, then we 
		 * should jump back to SEND_LIST_EXTENSIONS
		 */
        else if (NS_SUCCEEDED(rv) && pushAuth)
			m_nextState = SEND_LIST_EXTENSIONS;
		else
			/* Normal authentication */
			m_nextState = SEND_FIRST_NNTP_COMMAND;

		net_news_last_username_probably_valid = PR_TRUE;
		return(0); 
	  }
	else if (MK_NNTP_RESPONSE_AUTHINFO_CONT == m_responseCode)
	  {
		/* password required
		 */	
		char * command = 0;
		char * password = 0;
		char * cp;

		// mscott: I'm not sure why we need a pane in order to get the password....
		// commenting out for now because panes are going away!
#if 0
		if (cd->pane)
#else
		if (1)
#endif
		{
            m_newsgroup->GetPassword(&password);
#ifdef UNREADY_CODE
            password = HG63218 (password);
#endif
            m_newsgroup->SetPassword(NULL);
		}

        if (net_news_last_username_probably_valid 
			&& last_password
			&& last_password_hostname
			&& !PL_strcasecmp(last_password_hostname, m_hostName))
          {
#ifdef CACHE_NEWSGRP_PASSWORD
			if (cd->pane)
            m_newsgroup->GetPassword(&password);
            password = HG63218 (password);
#else
            NET_SACopy(&password, last_password);
#endif
          }
        else if ((cp = PL_strchr(m_hostName, '@')) != NULL)
          {
            /* in this case the username and possibly
             * the password are in the URL
             */
            char * colon;
            *cp = '\0';
    
            colon = PL_strchr(m_hostName, ':');
            if(colon)
			  {
                *colon = '\0';
    
            	NET_SACopy(&password, colon+1);
            	NET_SACopy(&last_password, colon+1);
            	NET_SACopy(&last_password_hostname, cp+1);

                *colon = ':';
			  }
    
            *cp = '@';
    
          }
		if (!password) 
		{
#if defined(CookiesAndSignons)
		  password = SI_PromptPassword
		      (ce->window_id,
		      XP_GetString
			  (XP_PLEASE_ENTER_A_PASSWORD_FOR_NEWS_SERVER_ACCESS),
		      m_hostName,
		      PR_TRUE, PR_TRUE);
#else
#ifdef UNREADY_CODE
			password = FE_PromptPassword(ce->window_id, XP_GetString(XP_PLEASE_ENTER_A_PASSWORD_FOR_NEWS_SERVER_ACCESS ) );
#endif
#endif
		  net_news_last_username_probably_valid = PR_FALSE;
		}
		  
		if(!password)	
		{
		  m_runningURL->SetErrorMessage(NET_ExplainErrorDetails(MK_NNTP_AUTH_FAILED, "Aborted by user"));
		  return(MK_NNTP_AUTH_FAILED);
		}
		else 
		{
		  NET_SACopy(&last_password, password);
		  NET_SACopy(&last_password_hostname, m_hostName);
		}

#ifdef CACHE_NEWSGRP_PASSWORD
        char *garbage_password;
        nsresult rv;
        rv = m_newsgroup->GetPassword(&garbage_password);
        if (!NS_SUCCEEDED(rv)) {
          PR_Free(garbage_password);
          munged_password = HG64643(password);
          m_newsgroup->SetPassword(munged_password);
		  PR_FREEIF(munged_password);
		}
#endif

		NET_SACopy(&command, "AUTHINFO pass ");
		NET_SACat(&command, password);
		NET_SACat(&command, CRLF);
	
		status = SendData(m_runningURL, command);

		PR_FREEIF(command);
		PR_FREEIF(password);

		m_nextState = NNTP_RESPONSE;
		m_nextStateAfterResponse = NNTP_PASSWORD_RESPONSE;
		SetFlag(NNTP_PAUSE_FOR_READ);

		return status;
	  }
	else
	{
		m_runningURL->SetErrorMessage(NET_ExplainErrorDetails(
									MK_NNTP_AUTH_FAILED,
									m_responseText ? m_responseText : ""));

#ifdef CACHE_NEWSGRP_PASSWORD
		if (cd->pane)
          m_newsgroup->SetUsername(NULL);
#endif
		net_news_last_username_probably_valid = PR_FALSE;

        return(MK_NNTP_AUTH_FAILED);
	  }
		
	PR_ASSERT(0); /* should never get here */
	return(-1);

}

PRInt32 nsNNTPProtocol::PasswordResponse()
{

    if (MK_NNTP_RESPONSE_AUTHINFO_OK == m_responseCode ||
        MK_NNTP_RESPONSE_AUTHINFO_SIMPLE_OK == m_responseCode) 
	  {
        /* successful login */
        nsresult rv = NS_OK;
        PRBool pushAuth;
		/* If we're here because the host demanded authentication before we
		 * even sent a single command, then jump back to the beginning of everything
		 */
        rv = m_newsHost->GetPushAuth(&pushAuth);
        
		if (!TestFlag(NNTP_READER_PERFORMED))
			m_nextState = NNTP_SEND_MODE_READER;
		/* If we're here because the host needs pushed authentication, then we 
		 * should jump back to SEND_LIST_EXTENSIONS
		 */
        else if (NS_SUCCEEDED(rv) && pushAuth)
			m_nextState = SEND_LIST_EXTENSIONS;
		else
			/* Normal authentication */
			m_nextState = SEND_FIRST_NNTP_COMMAND;

		net_news_last_username_probably_valid = PR_TRUE;
        rv = m_newsgroupList->ResetXOVER();
        return(0);
	  }
	else
	{
		m_runningURL->SetErrorMessage(NET_ExplainErrorDetails(
									MK_NNTP_AUTH_FAILED,
									m_responseText ? m_responseText : ""));

#ifdef CACHE_NEWSGRP_PASSWORD
		if (cd->pane)
          m_newsgroup->SetPassword(NULL);
#endif
        return(MK_NNTP_AUTH_FAILED);
	  }
		
	PR_ASSERT(0); /* should never get here */
	return(-1);
}

PRInt32 nsNNTPProtocol::DisplayNewsgroups()
{
	m_nextState = NEWS_DONE;
    ClearFlag(NNTP_PAUSE_FOR_READ);

	NNTP_LOG_NOTE(("about to display newsgroups. path: %s",m_path));

#if 0
	/* #### Now ignoring "news:alt.fan.*"
	   Need to open the root tree of the default news host and keep
	   opening one child at each level until we've exhausted the
	   wildcard...
	 */
	if(rv < 0)
       return(rv);  
	else
#endif
       return(MK_DATA_LOADED);  /* all finished */
}

PRInt32 nsNNTPProtocol::BeginNewsgroups()
{
	PRInt32 status = 0; 
	m_nextState = NNTP_NEWGROUPS;
#ifdef UNREADY_CODE
	NET_Progress(ce->window_id, XP_GetString(XP_PROGRESS_RECEIVE_NEWSGROUP));

	ce->bytes_received = 0;
#endif
	return(status);
}

PRInt32 nsNNTPProtocol::ProcessNewsgroups(nsIInputStream * inputStream, PRUint32 length)
{
	char *line, *s, *s1=NULL, *s2=NULL, *flag=NULL;
	PRInt32 oldest, youngest;
	PRUint32 status = 0;
    nsresult rv = NS_OK;
    
	PRBool pauseForMoreData = PR_FALSE;
	line = m_lineStreamBuffer->ReadNextLine(inputStream, status, pauseForMoreData);

	if(pauseForMoreData)
	{
		SetFlag(NNTP_PAUSE_FOR_READ);
		return 0;
	}

    if(!line)
        return(status);  /* no line yet */

    if(status<0)
    {
        m_runningURL->SetErrorMessage(NET_ExplainErrorDetails(MK_TCP_READ_ERROR, PR_GetOSError()));

        /* return TCP error
         */
        return MK_TCP_READ_ERROR;
    }

    /* End of list? 
	 */
    if (line[0]=='.' && line[1]=='\0')
	{
		ClearFlag(NNTP_PAUSE_FOR_READ);
		PRBool xactive=PR_FALSE;
		rv = m_newsHost->QueryExtension("XACTIVE",&xactive);
		if (NS_SUCCEEDED(rv) && xactive)
		{
          char *groupName;
          rv = m_newsHost->GetFirstGroupNeedingExtraInfo(&groupName);
		  if (NS_SUCCEEDED(rv) && m_newsgroup)
		  {
                rv = m_newsHost->FindGroup(groupName, getter_AddRefs(m_newsgroup));
                PR_ASSERT(NS_SUCCEEDED(rv));
				m_nextState = NNTP_LIST_XACTIVE;
#ifdef DEBUG_NEWS
				printf("listing xactive for %s\n", groupName);
#endif
				PR_FREEIF(line);
				return 0;
		  }
		}
		m_nextState = NEWS_DONE;

#ifdef UNREADY_CODE
		if(ce->bytes_received == 0)
		{
			/* #### no new groups */
		}
#endif

		PR_FREEIF(line);
		if(status > 0)
        	return MK_DATA_LOADED;
		else
        	return status;
    }
    else if (line [0] == '.' && line [1] == '.')
      /* The NNTP server quotes all lines beginning with "." by doubling it. */
      line++;

    /* almost correct
     */
    if(status > 1)
    {
#ifdef DEBUG_NEWS
	printf("received %d bytes\n", status);
#endif
#ifdef UNREADY_CODE
        ce->bytes_received += status;
        FE_GraphProgress(ce->window_id, ce->URL_s, ce->bytes_received, status, ce->URL_s->content_length);
#endif
    }

    /* format is "rec.arts.movies.past-films 7302 7119 y"
	 */
	s = PL_strchr (line, ' ');
	if (s)
	{
		*s = 0;
		s1 = s+1;
		s = PL_strchr (s1, ' ');
		if (s)
		{
			*s = 0;
			s2 = s+1;
			s = PL_strchr (s2, ' ');
			if (s)
			{
			  *s = 0;
			  flag = s+1;
			}
		 }
	}
	youngest = s2 ? atol(s1) : 0;
	oldest   = s1 ? atol(s2) : 0;

#ifdef UNREADY_CODE
	ce->bytes_received++;  /* small numbers of groups never seem to trigger this */
#endif
	m_newsHost->AddNewNewsgroup(line, oldest, youngest, flag, PR_FALSE);

    PRBool xactive=PR_FALSE;
    rv = m_newsHost->QueryExtension("XACTIVE",&xactive);
    if (NS_SUCCEEDED(rv) && xactive)
	{
      m_newsHost->SetGroupNeedsExtraInfo(line, PR_TRUE);
	}

	PR_FREEIF(line);
    return(status);
}

/* Ahhh, this like print's out the headers and stuff
 *
 * always returns 0
 */
	 
PRInt32 nsNNTPProtocol::BeginReadNewsList()
{
    m_nextState = NNTP_READ_LIST;
	PRInt32 status = 0;
#ifdef UNREADY_CODE
	NET_Progress(ce->window_id, XP_GetString(XP_PROGRESS_RECEIVE_NEWSGROUP));
#endif
	 
    return(status);
}

/* display a list of all or part of the newsgroups list
 * from the news server
 */

PRInt32 nsNNTPProtocol::ReadNewsList(nsIInputStream * inputStream, PRUint32 length)
{
    char * line;
    char * description;
    int i=0;
	PRUint32 status = 1;
	
	PRBool pauseForMoreData = PR_FALSE;
	line = m_lineStreamBuffer->ReadNextLine(inputStream, status, pauseForMoreData);

	if(pauseForMoreData)
	{
		SetFlag(NNTP_PAUSE_FOR_READ);
		return 0;
	}

    if(!line)
        return(status);  /* no line yet */

    if(status<0)
	{
        m_runningURL->SetErrorMessage(NET_ExplainErrorDetails(MK_TCP_READ_ERROR, PR_GetOSError()));
        /* return TCP error
         */
        return MK_TCP_READ_ERROR;
	}

            /* End of list? */
    if (line[0]=='.' && line[1]=='\0')
    {
	    nsresult rv;
		PRBool listpnames=PR_FALSE;
		rv = m_newsHost->QueryExtension("LISTPNAMES",&listpnames);
		if (NS_SUCCEEDED(rv) && listpnames)
			m_nextState = NNTP_LIST_PRETTY_NAMES;
		else
			m_nextState = DISPLAY_NEWSGROUPS;
        ClearFlag(NNTP_PAUSE_FOR_READ);
		PR_FREEIF(line);
        return 0;  
    }
	else if (line [0] == '.' && line [1] == '.')
	  /* The NNTP server quotes all lines beginning with "." by doubling it. */
	  line++;

    /* almost correct
     */
    if(status > 1)
    {
#ifdef DEBUG_NEWS
	printf("received %d bytes\n", status);
#endif
#ifdef UNREADY_CODE
    	ce->bytes_received += status;
        FE_GraphProgress(ce->window_id, ce->URL_s, ce->bytes_received, status, ce->URL_s->content_length);
#endif
	}
    
	 /* find whitespace seperator if it exits */
    for(i=0; line[i] != '\0' && !NET_IS_SPACE(line[i]); i++)
        ;  /* null body */

    if(line[i] == '\0')
        description = &line[i];
    else
        description = &line[i+1];

    line[i] = 0; /* terminate group name */

	/* store all the group names 
	 */
    m_newsHost->AddNewNewsgroup(line, 0, 0, "", PR_FALSE);
	PR_FREEIF(line);
    return(status);
}

/* start the xover command
 */

PRInt32 nsNNTPProtocol::BeginReadXover()
{
    PRInt32 count;     /* Response fields */
	PRInt32 status = 0; 

	/* Make sure we never close and automatically reopen the connection at this
	   point; we'll confuse libmsg too much... */

	SetFlag(NNTP_SOME_PROTOCOL_SUCCEEDED); 

	/* We have just issued a GROUP command and read the response.
	   Now parse that response to help decide which articles to request
	   xover data for.
	 */
    PR_sscanf(m_responseText,
		   "%d %d %d", 
		   &count, 
		   &m_firstPossibleArticle, 
		   &m_lastPossibleArticle);

	/* We now know there is a summary line there; make sure it has the
	   right numbers in it. */
    char *group_name = nsnull;
    m_newsgroup->GetName(&group_name);
    
    m_newsHost->DisplaySubscribedGroup(group_name,
                                          m_firstPossibleArticle,
                                          m_lastPossibleArticle,
                                          count, PR_TRUE);
    PR_Free(group_name);
	group_name = nsnull;
	if (status < 0) return status;

	m_numArticlesLoaded = 0;
	m_numArticlesWanted = net_NewsChunkSize > 0 ? net_NewsChunkSize : 1L << 30;

	m_nextState = NNTP_FIGURE_NEXT_CHUNK;
	ClearFlag(NNTP_PAUSE_FOR_READ);
	return 0;
}

PRInt32 nsNNTPProtocol::FigureNextChunk()
{
    nsresult rv = NS_OK;
	PRInt32 status = 0;

	const char * host_and_port = NULL;
	m_runningURL->GetHost(&host_and_port);

	if (!host_and_port) return MK_OUT_OF_MEMORY;

	if (m_firstArticle > 0) 
	{
      char *groupName;
      
	  rv = m_newsgroup->GetName(&groupName);

#ifdef DEBUG_sspitzer
      printf("leaking group name?\n");
#endif
      
      /* XXX - parse state stored in MSG_Pane cd->pane */
      if (NS_SUCCEEDED(rv))
          rv = m_newsHost->GetNewsgroupList(groupName, getter_AddRefs(m_newsgroupList));
      
#ifdef DEBUG_NEWS
	  printf("add to known articles:  %d - %d\n", m_firstArticle, m_lastArticle);
#endif

      if (NS_SUCCEEDED(rv) && m_newsgroupList) {
          rv = m_newsgroupList->AddToKnownArticles(m_firstArticle,
                                                 m_lastArticle);
      }

	  if (NS_FAILED(rv))
      {
		return status;
	  }
	}
										 

	if (m_numArticlesLoaded >= m_numArticlesWanted) 
	{
	  m_nextState = NEWS_PROCESS_XOVER;
	  ClearFlag(NNTP_PAUSE_FOR_READ);
	  return 0;
	}


    char *groupName = nsnull;
    
	if (!m_newsgroupList) {
	    rv = m_newsgroup->GetName(&groupName);
		if (NS_SUCCEEDED(rv))
			rv = m_newsHost->GetNewsgroupList(groupName, getter_AddRefs(m_newsgroupList));
	}
        
    if (NS_SUCCEEDED(rv) && m_newsgroupList) {
        rv = m_newsgroupList->GetRangeOfArtsToDownload(m_firstPossibleArticle,
                                              m_lastPossibleArticle,
                                              m_numArticlesWanted -
                                              m_numArticlesLoaded,
                                              &(m_firstArticle),
                                              &(m_lastArticle),
                                              &status);
	
	}

	if (NS_FAILED(rv)) {
	  return status;
	}

	if (m_firstArticle <= 0 || m_firstArticle > m_lastArticle) 
	{
	  /* Nothing more to get. */
	  m_nextState = NEWS_PROCESS_XOVER;
	  ClearFlag(NNTP_PAUSE_FOR_READ);
	  return 0;
	}

#ifdef DEBUG_NEWS
    printf("Chunk will be (%d-%d)\n", m_firstArticle, m_lastArticle);
#endif
    
	m_articleNumber = m_firstArticle;

    /* was MSG_InitXOVER() */
    if (m_newsgroupList) {
        rv = m_newsgroupList->InitXOVER(m_firstArticle, m_lastArticle);
	}

    /* convert nsresult->status */
    status = !NS_SUCCEEDED(rv);

	if (status < 0) 
	  return status;

	ClearFlag(NNTP_PAUSE_FOR_READ);
	if (TestFlag(NNTP_NO_XOVER_SUPPORT)) 
		m_nextState = NNTP_READ_GROUP;
	else 
		m_nextState = NNTP_XOVER_SEND;

	return 0;
}

PRInt32 nsNNTPProtocol::XoverSend()
{
	char outputBuffer[OUTPUT_BUFFER_SIZE];
	PRInt32 status = 0;

    PR_snprintf(outputBuffer, 
				OUTPUT_BUFFER_SIZE,
				"XOVER %d-%d" CRLF, 
				m_firstArticle, 
				m_lastArticle);

#ifdef DEBUG_NEWS
	printf("XOVER %d-%d\n", m_firstArticle, m_lastArticle);
#endif

	NNTP_LOG_WRITE(outputBuffer);

    m_nextState = NNTP_RESPONSE;
    m_nextStateAfterResponse = NNTP_XOVER_RESPONSE;
    SetFlag(NNTP_PAUSE_FOR_READ);
#ifdef UNREADY_CODE
	NET_Progress(ce->window_id, XP_GetString(XP_PROGRESS_RECEIVE_LISTARTICLES));
#endif

	status = SendData(m_runningURL, outputBuffer); 
	return status;
}

/* see if the xover response is going to return us data
 * if the proper code isn't returned then assume xover
 * isn't supported and use
 * normal read_group
 */

PRInt32 nsNNTPProtocol::ReadXoverResponse()
{
#ifdef TEST_NO_XOVER_SUPPORT
	m_responseCode = MK_NNTP_RESPONSE_CHECK_ERROR; /* pretend XOVER generated an error */
#endif

    if(m_responseCode != MK_NNTP_RESPONSE_XOVER_OK)
    {
        /* If we didn't get back "224 data follows" from the XOVER request,
		   then that must mean that this server doesn't support XOVER.  Or
		   maybe the server's XOVER support is busted or something.  So,
		   in that case, fall back to the very slow HEAD method.

		   But, while debugging here at HQ, getting into this state means
		   something went very wrong, since our servers do XOVER.  Thus
		   the assert.
         */
		/*PR_ASSERT (0);*/
		m_nextState = NNTP_READ_GROUP;
		SetFlag(NNTP_NO_XOVER_SUPPORT);
    }
    else
    {
        m_nextState = NNTP_XOVER;
    }

    return(0);  /* continue */
}

/* process the xover list as it comes from the server
 * and load it into the sort list.  
 */

PRInt32 nsNNTPProtocol::ReadXover(nsIInputStream * inputStream, PRUint32 length)
{
	char *line;
    nsresult rv;
	PRUint32 status = 1;

    PRBool pauseForMoreData = PR_FALSE;
	line = m_lineStreamBuffer->ReadNextLine(inputStream, status, pauseForMoreData);

	if(pauseForMoreData)
	{
		SetFlag(NNTP_PAUSE_FOR_READ);
		return 0;
	}

    if(!line)
		return(status);  /* no line yet or TCP error */

	if(status<0) 
	{
        m_runningURL->SetErrorMessage(NET_ExplainErrorDetails(MK_TCP_READ_ERROR, PR_GetOSError()));

        /* return TCP error
         */
        return MK_TCP_READ_ERROR;
	}

    if(line[0] == '.' && line[1] == '\0')
    {
		m_nextState = NNTP_FIGURE_NEXT_CHUNK;
		ClearFlag(NNTP_PAUSE_FOR_READ);
		PR_FREEIF(line);
		return(0);
    }
	else if (line [0] == '.' && line [1] == '.')
	  /* The NNTP server quotes all lines beginning with "." by doubling it. */
	  line++;

    /* almost correct
     */
    if(status > 1)
    {
#ifdef DEBUG_NEWS
	printf("received %d bytes\n", status);
#endif
#ifdef UNREADY_CODE
        ce->bytes_received += status;
        FE_GraphProgress(ce->window_id, ce->URL_s, ce->bytes_received, status,
						 ce->URL_s->content_length);
#endif
	}

	 rv = m_newsgroupList->ProcessXOVERLINE(line, &status);
	 PR_ASSERT(NS_SUCCEEDED(rv));

	m_numArticlesLoaded++;

	PR_FREEIF(line);
    return status; /* keep going */
}


/* Finished processing all the XOVER data.
 */

PRInt32 nsNNTPProtocol::ProcessXover()
{
    nsresult rv;
	PRInt32 status = 0;

    /* xover_parse_state stored in MSG_Pane cd->pane */
	if (!m_newsgroupList) {
		return NS_ERROR_NULL_POINTER;
	}

    rv = m_newsgroupList->FinishXOVERLINE(0,&status);

	if (NS_SUCCEEDED(rv) && status < 0) return status;

	m_nextState = NEWS_DONE;

    return(MK_DATA_LOADED);
}

PRInt32 nsNNTPProtocol::ReadNewsgroup()
{
	if(m_articleNumber > m_lastArticle)
    {  /* end of groups */

		m_nextState = NNTP_FIGURE_NEXT_CHUNK;
		ClearFlag(NNTP_PAUSE_FOR_READ);
		return(0);
    }
    else
    {
		char outputBuffer[OUTPUT_BUFFER_SIZE];
        PR_snprintf(outputBuffer, 
				   OUTPUT_BUFFER_SIZE,  
				   "HEAD %ld" CRLF, 
				   m_articleNumber++);
        m_nextState = NNTP_RESPONSE;
		m_nextStateAfterResponse = NNTP_READ_GROUP_RESPONSE;

        SetFlag(NNTP_PAUSE_FOR_READ);

        return SendData(m_runningURL, outputBuffer);
    }
}

/* See if the "HEAD" command was successful
 */

PRInt32 nsNNTPProtocol::ReadNewsgroupResponse()
{
  nsresult rv;
  
  if (m_responseCode == MK_NNTP_RESPONSE_ARTICLE_HEAD)
  {     /* Head follows - parse it:*/
	  m_nextState = NNTP_READ_GROUP_BODY;

	  if(m_messageID)
		*m_messageID = '\0';

	  /* Give the message number to the header parser. */
      rv = m_newsgroupList->ProcessNonXOVER(m_responseText);
      /* convert nsresult->status */
      return !NS_SUCCEEDED(rv);
  }
  else
  {
	  NNTP_LOG_NOTE(("Bad group header found!"));
	  m_nextState = NNTP_READ_GROUP;
	  return(0);
  }
}

/* read the body of the "HEAD" command
 */
PRInt32 nsNNTPProtocol::ReadNewsgroupBody(nsIInputStream * inputStream, PRUint32 length)
{
  char *line;
  nsresult rv;
  PRUint32 status = 1;

  PRBool pauseForMoreData = PR_FALSE;
  line = m_lineStreamBuffer->ReadNextLine(inputStream, status, pauseForMoreData);

	if(pauseForMoreData)
	{
		SetFlag(NNTP_PAUSE_FOR_READ);
		return 0;
	}

  /* if TCP error of if there is not a full line yet return
   */
  if(!line)
	return status;

  if(status < 0)
  {
	  m_runningURL->SetErrorMessage(NET_ExplainErrorDetails(MK_TCP_READ_ERROR, PR_GetOSError()));
	  /* return TCP error
	   */
	  return MK_TCP_READ_ERROR;
  }

  NNTP_LOG_NOTE(("read_group_body: got line: %s|",line));

  /* End of body? */
  if (line[0]=='.' && line[1]=='\0')
  {
	  m_nextState = NNTP_READ_GROUP;
	  ClearFlag(NNTP_PAUSE_FOR_READ);
  }
  else if (line [0] == '.' && line [1] == '.')
	/* The NNTP server quotes all lines beginning with "." by doubling it. */
	line++;

  rv = m_newsgroupList->ProcessNonXOVER(line);
  /* convert nsresult->status */
  PR_FREEIF(line);
  return !NS_SUCCEEDED(rv);
}

// sspitzer:  PostMessageInFile is copied from nsSmtpProtocol::SendMessageInFile()
// 
// mscott: after dogfood, make a note to move this type of function into a base
// utility class....
#define POST_DATA_BUFFER_SIZE 2048

PRInt32 nsNNTPProtocol::PostMessageInFile(const nsFilePath &filePath)
{
	if (filePath && *filePath)
	{
#ifdef DEBUG_NEWS
        printf("post this file: %s\n",(const char *)nsFileSpec(filePath));
#endif /* DEBUG_NEWS */
        
		nsInputFileStream * fileStream = new nsInputFileStream(nsFileSpec(filePath), PR_RDONLY, 00700);
		if (fileStream)
		{
			PRInt32 amtInBuffer = 0; 
			PRBool lastLineWasComplete = PR_TRUE;

			PRBool quoteLines = PR_TRUE;  // it is always true but I'd like to generalize this function and then it might not be
			char buffer[POST_DATA_BUFFER_SIZE];

            if (quoteLines /* || add_crlf_to_line_endings */)
            {
				char *line;
				char * b = buffer;
                PRInt32 bsize = POST_DATA_BUFFER_SIZE;
                amtInBuffer =  0;
                do {
					
					PRInt32 L = 0;
					if (fileStream->eof())
					{
						line = nsnull;
						break;
					}
					if (!fileStream->readline(b, bsize-5)) // if the readline returns false, jump out...
					{
						line = nsnull;
						break;
					}
					else
						line = b;

					L = PL_strlen(line);

					/* escape periods only if quote_lines_p is set
					*/
					if (quoteLines && lastLineWasComplete && line[0] == '.')
                    {
                      /* This line begins with "." so we need to quote it
                         by adding another "." to the beginning of the line.
                       */
						PRInt32 i;
						line[L+1] = 0;
						for (i = L; i > 0; i--)
							line[i] = line[i-1];
						L++;
                    }

					/* set default */
					lastLineWasComplete = PR_TRUE;

					if (L > 1 && line[L-2] == CR && line[L-1] == LF)
                    {
                        /* already ok */
                    }
					else if(L > 0 /* && (line[L-1] == LF || line[L-1] == CR) */)
                    {
                      /* only add the crlf if required
                       * we still need to do all the
                       * if comparisons here to know
                       * if the line was complete
                       */
                      if(/* add_crlf_to_line_endings */ PR_TRUE)
                      {
                          /* Change newline to CRLF. */
//                          L--;
                          line[L++] = CR;
                          line[L++] = LF;
                          line[L] = 0;
                      }
					}
					else
                    {
						line[L++] = CR;
                        line[L++] = LF;
                        line[L] = 0;
						lastLineWasComplete = PR_FALSE;
                    }

					bsize -= L;
					b += L;
					amtInBuffer += L;
				} while (line && bsize > 100);
              }

			SendData(m_runningURL, buffer); 
		}
	} // if filePath

    SetFlag(NNTP_PAUSE_FOR_READ);
    
	// for now, we are always done at this point..we aren't making multiple calls
	// to post data...

	// always issue a '.' and CRLF when we are done...
    PL_strcpy(m_dataBuf, CRLF "." CRLF);
	SendData(m_runningURL, m_dataBuf);
#ifdef UNREADY_CODE
    NET_Progress(CE_WINDOW_ID,
                 XP_GetString(XP_MESSAGE_SENT_WAITING_MAIL_REPLY));
#endif /* UNREADY_CODE */
    m_nextState = NNTP_RESPONSE;
    m_nextStateAfterResponse = NNTP_SEND_POST_DATA_RESPONSE;
    return(0);
}
    
PRInt32 nsNNTPProtocol::PostData()
{
    /* returns 0 on done and negative on error
     * positive if it needs to continue.
     */
#ifdef DEBUG_NEWS
    printf("nsNNTPProtocol::PostData()\n");
#endif
    nsresult rv = NS_OK;
    
    nsCOMPtr <nsINNTPNewsgroupPost> message;
    rv = m_runningURL->GetMessageToPost(getter_AddRefs(message));
    if (NS_SUCCEEDED(rv)) {
        nsFilePath *filePath;
        rv = message->GetPostMessageFile(&filePath);
        if (NS_SUCCEEDED(rv)) {
            PostMessageInFile(*filePath);
        }
     }

#ifdef UNREADY_CODE
    status = NET_WritePostData(ce->window_id, ce->URL_s,
                                  ce->socket,
								  &cd->write_post_data_data,
								  PR_TRUE);

    SetFlag(NNTP_PAUSE_FOR_READ);

    if(status == 0)
    {
        /* normal done
         */
        PL_strcpy(cd->output_buffer, CRLF "." CRLF);
        NNTP_LOG_WRITE(cd->output_buffer);
        status = (int) NET_BlockingWrite(ce->socket,
                                            cd->output_buffer,
                                            PL_strlen(cd->output_buffer));
		NNTP_LOG_WRITE(cd->output_buffer);

		NET_Progress(ce->window_id,
					XP_GetString(XP_MESSAGE_SENT_WAITING_NEWS_REPLY));

        NET_ClearConnectSelect(ce->window_id, ce->socket);
#ifdef XP_WIN
		if(cd->calling_netlib_all_the_time)
		{
			cd->calling_netlib_all_the_time = PR_FALSE;
#if 0
          /* this should be handled by NET_ClearCallNetlibAllTheTime */
			net_call_all_the_time_count--;
			if(net_call_all_the_time_count == 0)
#endif /* 0 */
				NET_ClearCallNetlibAllTheTime(ce->window_id,"mknews");
		}
#endif /* XP_WIN */
        NET_SetReadSelect(ce->window_id, ce->socket);
		ce->con_sock = 0;

        m_nextState = NNTP_RESPONSE;
        m_nextStateAfterResponse = NNTP_SEND_POST_DATA_RESPONSE;
        return(0);
      }

    return(status);
#endif /* UNREADY_CODE */
    return 0;
}


/* interpret the response code from the server
 * after the post is done
 */   
PRInt32 nsNNTPProtocol::PostDataResponse()
{
	if (m_responseCode != MK_NNTP_RESPONSE_POST_OK) 
	{
#ifdef UNREADY_CODE
	  ce->URL_s->error_msg =
		NET_ExplainErrorDetails(MK_NNTP_ERROR_MESSAGE, 
								m_responseText ? m_responseText : "");
	  if (m_responseCode == MK_NNTP_RESPONSE_POST_FAILED 
		  && MSG_GetPaneType(cd->pane) == MSG_COMPOSITIONPANE
		  && MSG_IsDuplicatePost(cd->pane) &&
		  MSG_GetCompositionMessageID(cd->pane)) {
		/* The news server won't let us post.  We suspect that we're submitting
		   a duplicate post, and that's why it's failing.  So, let's go see
		   if there really is a message out there with the same message-id.
		   If so, we'll just silently pretend everything went well. */
		PR_snprintf(cd->output_buffer, OUTPUT_BUFFER_SIZE, "STAT %s" CRLF,
					MSG_GetCompositionMessageID(cd->pane));
		m_nextState = NNTP_RESPONSE;
		m_nextStateAfterResponse = NNTP_CHECK_FOR_MESSAGE;
		NNTP_LOG_WRITE(cd->output_buffer);
		return (int) NET_BlockingWrite(ce->socket, cd->output_buffer,
									   PL_strlen(cd->output_buffer));
	  }

	  MSG_ClearCompositionMessageID(cd->pane); /* So that if the user tries
													  to just post again, we
													  won't immediately decide
													  that this was a duplicate
													  message and ignore the
													  error. */
#endif
	  m_nextState = NEWS_ERROR;
	  return(MK_NNTP_ERROR_MESSAGE);
	}
    m_nextState = NEWS_POST_DONE;
	ClearFlag(NNTP_PAUSE_FOR_READ);
    return(MK_DATA_LOADED);
}

PRInt32 nsNNTPProtocol::CheckForArticle()
{
  m_nextState = NEWS_ERROR;
  if (m_responseCode >= 220 && m_responseCode <= 223) {
	/* Yes, this article is already there, we're all done. */
	return MK_DATA_LOADED;
  } 
  else 
  {
	/* The article isn't there, so the failure we had earlier wasn't due to
	   a duplicate message-id.  Return the error from that previous
	   posting attempt (which is already in ce->URL_s->error_msg). */
#ifdef UNREADY_CODE
	MSG_ClearCompositionMessageID(cd->pane);
#endif
	return MK_NNTP_ERROR_MESSAGE;
  }
}

#define NEWS_GROUP_DISPLAY_FREQ		20

PRInt32 nsNNTPProtocol::DisplayNewsRC()
{
    nsresult rv;
	PRInt32 status = 0; 

	if(!TestFlag(NNTP_NEWSRC_PERFORMED))
	{
		SetFlag(NNTP_NEWSRC_PERFORMED);
        rv = m_newsHost->GetNumGroupsNeedingCounts(&m_newsRCListCount);
	}
	
	PR_FREEIF(m_currentGroup);
    rv = m_newsHost->GetFirstGroupNeedingCounts(&m_currentGroup);


	if(NS_SUCCEEDED(rv) && m_currentGroup)
    {
		/* send group command to server
		 */
		PRInt32 percent;

		char outputBuffer[OUTPUT_BUFFER_SIZE];

		PR_snprintf(outputBuffer, OUTPUT_BUFFER_SIZE, "GROUP %.512s" CRLF, m_currentGroup);
		status = SendData(m_runningURL, outputBuffer);

		percent = (m_newsRCListCount) ?
					(PRInt32) (100.0 * ( (double)m_newsRCListIndex / (double)m_newsRCListCount )) :
					0;
#ifdef UNREADY_CODE
		FE_SetProgressBarPercent (ce->window_id, percent);
#endif
		
		/* only update every 20 groups for speed */
		if ((m_newsRCListCount <= NEWS_GROUP_DISPLAY_FREQ) || (m_newsRCListIndex % NEWS_GROUP_DISPLAY_FREQ) == 0 ||
									(m_newsRCListIndex == m_newsRCListCount))
		{
			char thisGroup[20];
			char totalGroups[20];
			char *statusText;
			
			PR_snprintf (thisGroup, sizeof(thisGroup), "%ld", (long) m_newsRCListIndex);
			PR_snprintf (totalGroups, sizeof(totalGroups), "%ld", (long) m_newsRCListCount);
#ifdef UNREADY_CODE
			statusText = PR_smprintf (XP_GetString(XP_THERMO_PERCENT_FORM), thisGroup, totalGroups);
#else
			statusText = nsnull;
#endif
			if (statusText)
			{
#ifdef UNREADY_CODE
				FE_Progress (ce->window_id, statusText);
#endif
				PR_Free(statusText);
			}
		}
		
		m_newsRCListIndex++;

		SetFlag(NNTP_PAUSE_FOR_READ);
		m_nextState = NNTP_RESPONSE;
		m_nextStateAfterResponse = NEWS_DISPLAY_NEWS_RC_RESPONSE;
    }
	else
	{
		if (m_newsRCListCount)
		{
#ifdef UNREADY_CODE
			FE_SetProgressBarPercent (ce->window_id, -1);
#endif
			m_newsRCListCount = 0;
		}
		else if (m_responseCode == MK_NNTP_RESPONSE_LIST_OK)  
		{
			/*
			 * 5-9-96 jefft 
			 * If for some reason the news server returns an empty 
			 * newsgroups list with a nntp response code MK_NNTP_RESPONSE_LIST_OK -- list of
			 * newsgroups follows. We set status to MK_EMPTY_NEWS_LIST
			 * to end the infinite dialog loop.
			 */
			status = MK_EMPTY_NEWS_LIST;
		}
		m_nextState = NEWS_DONE;
	
		if(status > -1)
		  return MK_DATA_LOADED; 
		else
		  return(status);
	}

	return(status); /* keep going */

}

/* Parses output of GROUP command */
PRInt32 nsNNTPProtocol::DisplayNewsRCResponse()
{
	PRInt32 status = 0;
    if(m_responseCode == MK_NNTP_RESPONSE_GROUP_SELECTED)
    {
		char *num_arts = 0, *low = 0, *high = 0, *group = 0;
		PRInt32 first_art, last_art;

		/* line looks like:
		 *     211 91 3693 3789 comp.infosystems
		 */

		num_arts = m_responseText;
		low = PL_strchr(num_arts, ' ');

		if(low)
		{
			first_art = atol(low);
			*low++ = '\0';
			high= PL_strchr(low, ' ');
		}
		if(high)
		{
			*high++ = '\0';
			group = PL_strchr(high, ' ');
		}
		if(group)
		{
			*group++ = '\0';
			/* the group name may be contaminated by "group selected" at
			   the end.  This will be space separated from the group name.
			   If a space is found in the group name terminate at that
			   point. */
			strtok(group, " ");
			last_art = atol(high);
		}

        m_newsHost->DisplaySubscribedGroup(group,
                                              low ? atol(low) : 0,
                                              high ? atol(high) : 0,
                                              atol(num_arts), PR_FALSE);
		if (status < 0)
		  return status;
	  }
	  else if (m_responseCode == MK_NNTP_RESPONSE_GROUP_NO_GROUP)
	  {
          m_newsHost->GroupNotFound(m_currentGroup, PR_FALSE);
	  }
	  /* it turns out subscribe ui depends on getting this displaysubscribedgroup call,
	     even if there was an error.
	  */
	  if(m_responseCode != MK_NNTP_RESPONSE_GROUP_SELECTED)
	  {
		/* only on news server error or when zero articles
		 */
        m_newsHost->DisplaySubscribedGroup(m_currentGroup,
	                                             0, 0, 0, PR_FALSE);
	  }

	m_nextState = NEWS_DISPLAY_NEWS_RC;
		
	return 0;
}

PRInt32 nsNNTPProtocol::StartCancel()
{
  PRInt32 status = SendData(m_runningURL, NNTP_CMD_POST);

  m_nextState = NNTP_RESPONSE;
  m_nextStateAfterResponse = NEWS_DO_CANCEL;
  SetFlag(NNTP_PAUSE_FOR_READ);
  return (status);
}

PRInt32 nsNNTPProtocol::Cancel()
{
	int status = 0;
    nsresult rv = NS_OK;
	char *id, *subject, *newsgroups, *distribution, *other_random_headers, *body;
	char *from, *old_from;
	int L;
#ifdef USE_LIBMSG
	MSG_CompositionFields *fields = NULL;
#endif 

  /* #### Should we do a more real check than this?  If the POST command
	 didn't respond with "MK_NNTP_RESPONSE_POST_SEND_NOW Ok", then it's not ready for us to throw a
	 message at it...   But the normal posting code doesn't do this check.
	 Why?
   */
  PR_ASSERT (m_responseCode == MK_NNTP_RESPONSE_POST_SEND_NOW);

  // These shouldn't be set yet, since the headers haven't been "flushed"
  // "Distribution: " doesn't appear to be required, so
  // don't assert on m_cancelDistribution
  PR_ASSERT (m_cancelID &&
			 m_cancelFromHdr &&
			 m_cancelNewsgroups);

  newsgroups = m_cancelNewsgroups;
  distribution = m_cancelDistribution;
  old_from = m_cancelFromHdr;
  id = m_cancelID;

  NS_WITH_SERVICE(nsINetSupportDialogService,dialog,kCNetSupportDialogCID,&rv);
  if (NS_FAILED(rv)) return -1;  /* unable to get the dialog service */

  PR_ASSERT (id && newsgroups);
  if (!id || !newsgroups) return -1; /* "unknown error"... */

  m_cancelNewsgroups = nsnull;
  m_cancelDistribution = nsnull;
  m_cancelFromHdr = nsnull;
  m_cancelID = nsnull;

  L = PL_strlen (id);
  
#ifdef UNREADY_CODE
  from = MIME_MakeFromField ();
#else
  // get the current identity from the news session....
  NS_WITH_SERVICE(nsIMsgMailSession,newsSession,kCMsgMailSessionCID,&rv);
  if (NS_SUCCEEDED(rv) && newsSession) {
        nsCOMPtr<nsIMsgIdentity> identity; 
	rv = newsSession->GetCurrentIdentity(getter_AddRefs(identity));
        if (NS_SUCCEEDED(rv) && identity) {
        	identity->GetEmail(&from);
	}
   }
#ifdef DEBUG_NEWS
   printf("post the cancel message as %s\n",from);
#endif /* DEBUG_NEWS */
#endif /* UNREADY_CODE */

  subject = (char *) PR_Malloc (L + 20);
  other_random_headers = (char *) PR_Malloc (L + 20);
  body = (char *) PR_Malloc (PL_strlen (XP_AppCodeName) + 100);
  
  // i18N people:  don't panic because these are eOneByte strings.  as soon
  // as I get string bundles working, these will go away.
  nsString alertText("",eOneByte);
  nsString confirmText(UNTIL_STRING_BUNDLES_MK_NNTP_CANCEL_CONFIRM, eOneByte);

  PRInt32 confirmCancelResult = 0;

  /* Make sure that this loser isn't cancelling someone else's posting.
	 Yes, there are occasionally good reasons to do so.  Those people
	 capable of making that decision (news admins) have other tools with
	 which to cancel postings (like telnet.)
     
	 Don't do this if server tells us it will validate user. DMB 3/19/97
   */
  PRBool cancelchk=PR_FALSE;
  rv = m_newsHost->QueryExtension("CANCELCHK",&cancelchk);
  if (NS_SUCCEEDED(rv) && !cancelchk) {
#ifdef DEBUG_NEWS
      printf("CANCELCHK not supported\n");
#endif
      nsCOMPtr<nsIMsgHeaderParser> parser;
      PRBool ok = PR_FALSE;
                  
      rv = nsComponentManager::CreateInstance(kCHeaderParserCID,
                                              nsnull,
                                              nsIMsgHeaderParser::GetIID(),
                                              getter_AddRefs(parser));
      
      if (NS_SUCCEEDED(rv))  {
#ifdef DEBUG_NEWS
          printf("got a header parser...\n");
#endif
          char *us = nsnull;
          char *them = nsnull;
          nsresult rv1 = parser->ExtractHeaderAddressMailboxes(nsnull, from, &us);
          nsresult rv2 = parser->ExtractHeaderAddressMailboxes(nsnull, old_from, &them);
#ifdef DEBUG_sspitzer
          printf("us = %s, them = %s\n", us, them);
#endif
          ok = (NS_SUCCEEDED(rv1) && NS_SUCCEEDED(rv2) && !PL_strcasecmp(us, them));
          
          if (NS_SUCCEEDED(rv1)) PR_Free(us);
          if (NS_SUCCEEDED(rv2)) PR_Free(them);
      }
      if (!ok) {
          alertText = UNTIL_STRING_BUNDLES_MK_NNTP_CANCEL_DISALLOWED;
          if (dialog) {
              // until #7770 is fixed, we can't do dialogs on Linux from here
#if defined(BUG_7770_FIXED)
              rv = dialog->Alert(alertText);
#else
              printf("%s\n",alertText.GetBuffer());
#endif /* BUG_7770_FIXED */
          }
          
          status = MK_NNTP_CANCEL_DISALLOWED;
          m_runningURL->SetErrorMessage(PL_strdup("not implemented"));
          m_nextState = NEWS_ERROR; /* even though it worked */
          ClearFlag(NNTP_PAUSE_FOR_READ);
          goto FAIL;
      }
  }
  else {
#ifdef DEBUG_NEWS
      printf("CANCELCHK supported, don't do the us vs. them test\n");
#endif
  }
  
  /* Last chance to cancel the cancel.
   */
  if (dialog) {
      // until #7770 is fixed, we can't do dialogs on UNIX from here
#if defined(BUG_7770_FIXED)
      rv = dialog->Confirm(confirmText, &confirmCancelResult);
#else
      printf("%s\n", confirmText.GetBuffer());
      confirmCancelResult = 1;
#endif /* BUG_7770_FIXED */
  }
  
  if (confirmCancelResult != 1) {
      // they canceled the cancel
      status = MK_NNTP_NOT_CANCELLED;
      goto FAIL;
  }  
  
  if (!from || !subject || !other_random_headers || !body) {
	  status = MK_OUT_OF_MEMORY;
	  goto FAIL;
  }
  
  PL_strcpy (subject, "cancel ");
  PL_strcat (subject, id);

  PL_strcpy (other_random_headers, "Control: cancel ");
  PL_strcat (other_random_headers, id);
  PL_strcat (other_random_headers, CRLF);
  if (distribution) {
	  PL_strcat (other_random_headers, "Distribution: ");
	  PL_strcat (other_random_headers, distribution);
	  PL_strcat (other_random_headers, CRLF);
  }

  PL_strcpy (body, "This message was cancelled from within ");
  PL_strcat (body, XP_AppCodeName);
  PL_strcat (body, "." CRLF);
  
#ifdef USE_LIBMSG
  fields = MSG_CreateCompositionFields(from, 0, 0, 0, 0, 0, newsgroups,
									   0, 0, subject, id, other_random_headers,
									   0, 0, news_url);
#endif 
  
  m_cancelStatus = 0;

  {
    /* NET_BlockingWrite() should go away soon? I think. */
    /* The following are what we really need to cancel a posted message */
    char *data;
    data = PR_smprintf("From: %s" CRLF
                       "Newsgroups: %s" CRLF
                       "Subject: %s" CRLF
                       "References: %s" CRLF
                       "%s" CRLF /* other_random_headers */
                       "%s"     /* body */
                       CRLF "." CRLF CRLF, /* trailing SMTP "." */
                       from, newsgroups, subject, id,
                       other_random_headers, body);
    
	status = SendData(m_runningURL, data);
    PR_Free (data);
    if (status < 0) {
		m_runningURL->SetErrorMessage(NET_ExplainErrorDetails(MK_TCP_WRITE_ERROR, status));
		goto FAIL;
	}

    SetFlag(NNTP_PAUSE_FOR_READ);
	m_nextState = NNTP_RESPONSE;
	m_nextStateAfterResponse = NNTP_SEND_POST_DATA_RESPONSE;

    alertText = UNTIL_STRING_BUNDLES_MK_MSG_MESSAGE_CANCELLED;
    if (dialog) {
        // until #7770 is fixed, we can't do dialogs on Linux from here
#if defined(BUG_7770_FIXED)
        rv = dialog->Alert(alertText);
#else
        printf("%s\n", alertText.GetBuffer());
#endif /* BUG_7770_FIXED */
    }

    // just me for now...
#ifdef DEBUG_sspitzer
    // delete the message from the db here.
    nsMsgKey key = nsMsgKey_None;
    rv = m_runningURL->GetMessageKey(&key);
    PR_ASSERT(NS_SUCCEEDED(rv));
    char *newsgroupname = nsnull;
    rv = m_runningURL->GetNewsgroupName(&newsgroupname);
    PR_ASSERT(NS_SUCCEEDED(rv) && newsgroupname);
    printf("delete %lu from %s\n",key,newsgroupname);
#endif
  }
    
FAIL:
  PR_FREEIF (id);
  PR_FREEIF (from);
  PR_FREEIF (old_from);
  PR_FREEIF (subject);
  PR_FREEIF (newsgroups);
  PR_FREEIF (distribution);
  PR_FREEIF (other_random_headers);
  PR_FREEIF (body);
  PR_FREEIF (m_cancelMessageFile);
  
#ifdef USE_LIBMSG
  if (fields)
	  MSG_DestroyCompositionFields(fields);
#endif 
  
  return status;
}
  
PRInt32 nsNNTPProtocol::XPATSend()
{
	int status = 0;
	char *thisTerm = NULL;
    
#ifdef UNREADY_CODE
	if (cd->current_search &&
		(thisTerm = PL_strchr(cd->current_search, '/')) != NULL) {
#else
    if (1) {
#endif
		/* extract the XPAT encoding for one query term */
        /* char *next_search = NULL; */
		char *command = NULL;
		char *unescapedCommand = NULL;
		char *endOfTerm = NULL;
		NET_SACopy (&command, ++thisTerm);
		endOfTerm = PL_strchr(command, '/');
		if (endOfTerm)
			*endOfTerm = '\0';
		NET_SACat(&command, CRLF);
	
		unescapedCommand = MSG_UnEscapeSearchUrl(command);

		/* send one term off to the server */
		NNTP_LOG_WRITE(command);
		status = SendData(m_runningURL, unescapedCommand);

		m_nextState = NNTP_RESPONSE;
		m_nextStateAfterResponse = NNTP_XPAT_RESPONSE;
	    SetFlag(NNTP_PAUSE_FOR_READ);

		PR_Free(command);
		PR_Free(unescapedCommand);
	}
	else
	{
		m_nextState = NEWS_DONE;
		status = MK_DATA_LOADED;
	}
	return status;
}

PRInt32 nsNNTPProtocol::XPATResponse(nsIInputStream * inputStream, PRUint32 length)
{
	char *line;
	PRUint32 status = 1; 

	if (m_responseCode != MK_NNTP_RESPONSE_XPAT_OK)
	{
#ifdef UNREADY_CODE
		ce->URL_s->error_msg  = NET_ExplainErrorDetails(MK_NNTP_ERROR_MESSAGE, m_responseText);
#endif
    	m_nextState = NNTP_ERROR;
		ClearFlag(NNTP_PAUSE_FOR_READ);
		return MK_NNTP_SERVER_ERROR;
	}

	PRBool pauseForMoreData = PR_FALSE;
	line = m_lineStreamBuffer->ReadNextLine(inputStream, status, pauseForMoreData);

	NNTP_LOG_READ(line);

	if(pauseForMoreData)
	{
		SetFlag(NNTP_PAUSE_FOR_READ);
		return 0;
	}

	if (line)
	{
		if (line[0] != '.')
		{
			long articleNumber;
			PR_sscanf(line, "%ld", &articleNumber);
#ifdef UNREADY_CODE
			MSG_AddNewsXpatHit (ce->window_id, (PRUint32) articleNumber);
#endif
		}
		else
		{
			/* set up the next term for next time around */
#ifdef UNREADY_CODE
			char *nextTerm = PL_strchr(cd->current_search, '/');

			if (nextTerm)
				cd->current_search = ++nextTerm;
			else
				cd->current_search = NULL;
#endif

			m_nextState = NNTP_XPAT_SEND;
			ClearFlag(NNTP_PAUSE_FOR_READ);
			PR_FREEIF(line);
			return 0;
		}
	}
	PR_FREEIF(line);
	return 0;
}

PRInt32 nsNNTPProtocol::ListPrettyNames()
{

    char *group_name;
	char outputBuffer[OUTPUT_BUFFER_SIZE];
	PRInt32 status = 0; 

    nsresult rv = m_newsgroup->GetName(&group_name);
	PR_snprintf(outputBuffer, 
			OUTPUT_BUFFER_SIZE, 
			"LIST PRETTYNAMES %.512s" CRLF,
            NS_SUCCEEDED(rv) ? group_name : "");
    
	status = SendData(m_runningURL, outputBuffer);
#ifdef DEBUG_NEWS
	printf(outputBuffer);
#endif
	m_nextState = NNTP_RESPONSE;
	m_nextStateAfterResponse = NNTP_LIST_PRETTY_NAMES_RESPONSE;

	return status;
}

PRInt32 nsNNTPProtocol::ListPrettyNamesResponse(nsIInputStream * inputStream, PRUint32 length)
{
	char *line;
	char *prettyName;
	PRUint32 status = 0;

	if (m_responseCode != MK_NNTP_RESPONSE_LIST_OK)
	{
		m_nextState = DISPLAY_NEWSGROUPS;
/*		m_nextState = NEWS_DONE; */
		ClearFlag(NNTP_PAUSE_FOR_READ);
		return 0;
	}

	PRBool pauseForMoreData = PR_FALSE;
	line = m_lineStreamBuffer->ReadNextLine(inputStream, status, pauseForMoreData);

	NNTP_LOG_READ(line);

	if(pauseForMoreData)
	{
		SetFlag(NNTP_PAUSE_FOR_READ);
		return 0;
	}

	if (line)
	{
		if (line[0] != '.')
		{
			int i;
			/* find whitespace seperator if it exits */
			for (i=0; line[i] != '\0' && !NET_IS_SPACE(line[i]); i++)
				;  /* null body */

			if(line[i] == '\0')
				prettyName = &line[i];
			else
				prettyName = &line[i+1];

			line[i] = 0; /* terminate group name */
			if (i > 0)
              m_newsHost->SetPrettyName(line,prettyName);
#ifdef DEBUG_NEWS 
			printf("adding pretty name %s\n", prettyName);
#endif
		}
		else
		{
			m_nextState = DISPLAY_NEWSGROUPS;	/* this assumes we were doing a list */
/*			m_nextState = NEWS_DONE;	 */ /* ### dmb - don't really know */
			ClearFlag(NNTP_PAUSE_FOR_READ);
			PR_FREEIF(line);
			return 0;
		}
	}
	PR_FREEIF(line);
	return 0;
}

PRInt32 nsNNTPProtocol::ListXActive()
{ 
	char *group_name;
    nsresult rv = m_newsgroup->GetName(&group_name);
	PRInt32 status = 0;
	char outputBuffer[OUTPUT_BUFFER_SIZE];

    if (NS_FAILED(rv)) group_name=NULL;
    
	PR_snprintf(outputBuffer, 
			OUTPUT_BUFFER_SIZE, 
			"LIST XACTIVE %.512s" CRLF,
            group_name);
	status = SendData(m_runningURL, outputBuffer);

	m_nextState = NNTP_RESPONSE;
	m_nextStateAfterResponse = NNTP_LIST_XACTIVE_RESPONSE;

	return status;
}

PRInt32 nsNNTPProtocol::ListXActiveResponse(nsIInputStream * inputStream, PRUint32 length)
{
	char *line;
	PRUint32 status = 0;

	PR_ASSERT(m_responseCode == MK_NNTP_RESPONSE_LIST_OK);
	if (m_responseCode != MK_NNTP_RESPONSE_LIST_OK)
	{
		m_nextState = DISPLAY_NEWSGROUPS;
/*		m_nextState = NEWS_DONE; */
		ClearFlag(NNTP_PAUSE_FOR_READ);
		return MK_DATA_LOADED;
	}

	PRBool pauseForMoreData = PR_FALSE;
	line = m_lineStreamBuffer->ReadNextLine(inputStream, status, pauseForMoreData);

#ifdef DEBUG_NEWS
	printf("NNTPLOG: %s\n", line);
#endif
	if(pauseForMoreData)
	{
		SetFlag(NNTP_PAUSE_FOR_READ);
		return 0;
	}

	 /* almost correct */
	if(status > 1)
	{
#ifdef DEBUG_NEWS
		printf("received %d bytes\n", status);
#endif
#ifdef UNREADY_CODE
		ce->bytes_received += status;
		FE_GraphProgress(ce->window_id, ce->URL_s, ce->bytes_received, status, ce->URL_s->content_length);
#endif
	}

	if (line)
	{
		if (line[0] != '.')
		{
			char *s = line;
			/* format is "rec.arts.movies.past-films 7302 7119 csp"
			 */
			while (*s && !NET_IS_SPACE(*s))
				s++;
			if (s)
			{
				char flags[32];	/* ought to be big enough */
				*s = 0;
				PR_sscanf(s + 1,
					   "%d %d %31s", 
					   &m_firstPossibleArticle, 
					   &m_lastPossibleArticle,
					   flags);

                m_newsHost->AddNewNewsgroup(line,
                                          m_firstPossibleArticle,
                                          m_lastPossibleArticle, flags, PR_TRUE);
				/* we're either going to list prettynames first, or list
                   all prettynames every time, so we won't care so much
                   if it gets interrupted. */
#ifdef DEBUG_NEWS 
				printf("got xactive for %s of %s\n", line, flags);
#endif
				/*	This isn't required, because the extra info is
                    initialized to false for new groups. And it's
                    an expensive call.
				*/
				/* MSG_SetGroupNeedsExtraInfo(cd->host, line, PR_FALSE); */
			}
		}
		else
		{
          nsresult rv;
          PRBool xactive=PR_FALSE;
          rv = m_newsHost->QueryExtension("XACTIVE",&xactive);
          if (m_typeWanted == NEW_GROUPS &&
              NS_SUCCEEDED(rv) && xactive)
			{
                nsCOMPtr <nsINNTPNewsgroup> old_newsgroup;
                old_newsgroup = m_newsgroup;
                char *groupName;
                
                m_newsHost->GetFirstGroupNeedingExtraInfo(&groupName);
                m_newsHost->FindGroup(groupName, getter_AddRefs(m_newsgroup));
                // see if we got a different group
                if (old_newsgroup && m_newsgroup &&
                    (old_newsgroup != m_newsgroup))
                /* make sure we're not stuck on the same group */
                {
#ifdef DEBUG_NEWS
					printf("listing xactive for %s\n", groupName);
#endif
					m_nextState = NNTP_LIST_XACTIVE;
			        ClearFlag(NNTP_PAUSE_FOR_READ); 
					PR_FREEIF(line);
					return 0;
				}
				else
                {
                    m_newsgroup = null_nsCOMPtr();
				}
			}
            PRBool listpname;
            rv = m_newsHost->QueryExtension("LISTPNAME",&listpname);
            if (NS_SUCCEEDED(rv) && listpname)
				m_nextState = NNTP_LIST_PRETTY_NAMES;
			else
				m_nextState = DISPLAY_NEWSGROUPS;	/* this assumes we were doing a list - who knows? */
/*			m_nextState = NEWS_DONE;	 */ /* ### dmb - don't really know */
			ClearFlag(NNTP_PAUSE_FOR_READ);
			PR_FREEIF(line);
			return 0;
		}
	}
	PR_FREEIF(line);
	return 0;
}

PRInt32 nsNNTPProtocol::ListGroup()
{
    nsresult rv;
    char *group_name;
	char outputBuffer[OUTPUT_BUFFER_SIZE];
	PRInt32 status = 0; 
    rv = m_newsgroup->GetName(&group_name);
    
	PR_snprintf(outputBuffer, 
			OUTPUT_BUFFER_SIZE, 
			"listgroup %.512s" CRLF,
                group_name);
    
    rv = nsComponentManager::CreateInstance(kNNTPArticleListCID,
                                            nsnull,
                                            nsINNTPArticleList::GetIID(),
                                            getter_AddRefs(m_articleList));
    if (NS_FAILED(rv)) return -1;  // ???

    m_articleList->Initialize(m_newsHost, m_newsgroup);
	
	status = SendData(m_runningURL, outputBuffer); 

	m_nextState = NNTP_RESPONSE;
	m_nextStateAfterResponse = NNTP_LIST_GROUP_RESPONSE;

	return status;
}

PRInt32 nsNNTPProtocol::ListGroupResponse(nsIInputStream * inputStream, PRUint32 length)
{
	char *line;
	PRUint32 status = 0;

	PR_ASSERT(m_responseCode == MK_NNTP_RESPONSE_GROUP_SELECTED);
	if (m_responseCode != MK_NNTP_RESPONSE_GROUP_SELECTED)
	{
		m_nextState = NEWS_DONE; 
		ClearFlag(NNTP_PAUSE_FOR_READ);
		return MK_DATA_LOADED;
	}

	PRBool pauseForMoreData = PR_FALSE;
	line = m_lineStreamBuffer->ReadNextLine(inputStream, status, pauseForMoreData);

	NNTP_LOG_READ(line);

	if(pauseForMoreData)
	{
		SetFlag(NNTP_PAUSE_FOR_READ);
		return 0;
	}

	if (line)
	{
		if (line[0] != '.')
		{
			nsMsgKey found_id = nsMsgKey_None;
            nsresult rv;
			PR_sscanf(line, "%ld", &found_id);
            
            rv = m_articleList->AddArticleKey(found_id);
		}
		else
		{
			m_nextState = NEWS_DONE;	 /* ### dmb - don't really know */
			ClearFlag(NNTP_PAUSE_FOR_READ); 
			PR_FREEIF(line);
			return 0;
		}
	}
	PR_FREEIF(line);
	return 0;
}


PRInt32 nsNNTPProtocol::Search()
{
	PR_ASSERT(PR_FALSE);
	return 0;
}

PRInt32 nsNNTPProtocol::SearchResponse()
{
    if (MK_NNTP_RESPONSE_TYPE(m_responseCode) == MK_NNTP_RESPONSE_TYPE_OK)
		m_nextState = NNTP_SEARCH_RESULTS;
	else
		m_nextState = NEWS_DONE;
	ClearFlag(NNTP_PAUSE_FOR_READ);
	return 0;
}

PRInt32 nsNNTPProtocol::SearchResults(nsIInputStream *inputStream, PRUint32 length)
{
	char *line = NULL;
	PRUint32 status = 1;

	PRBool pauseForMoreData = PR_FALSE;
	line = m_lineStreamBuffer->ReadNextLine(inputStream, status, pauseForMoreData);

	if(pauseForMoreData)
	{
		SetFlag(NNTP_PAUSE_FOR_READ);
		return 0;
	}
	if (!line)
		return status;  /* no line yet */
	if (status < 0)
	{
		m_runningURL->SetErrorMessage(NET_ExplainErrorDetails(MK_TCP_READ_ERROR, PR_GetOSError()));

		/* return TCP error */
		return MK_TCP_READ_ERROR;
	}

	if ('.' != line[0])
	{
#ifdef UNREADY_CODE
		MSG_AddNewsSearchHit (ce->window_id, line);
#endif
	}
	else
	{
		/* all overview lines received */
		m_nextState = NEWS_DONE;
		ClearFlag(NNTP_PAUSE_FOR_READ);
	}
	PR_FREEIF(line);
	return status;
}

/* Sets state for the transfer. This used to be known as net_setup_news_stream */
PRInt32 nsNNTPProtocol::SetupForTransfer()
{ 
    if (m_typeWanted == NEWS_POST)
	{
        m_nextState = NNTP_SEND_POST_DATA;
#ifdef UNREADY_CODE
		NET_Progress(ce->window_id, XP_GetString(MK_MSG_DELIV_NEWS));
#endif
	}
    else if(m_typeWanted == LIST_WANTED)
	{
		if (TestFlag(NNTP_USE_FANCY_NEWSGROUP))
			m_nextState = NNTP_LIST_XACTIVE_RESPONSE;
		else
			m_nextState = NNTP_READ_LIST_BEGIN;
	}
    else if(m_typeWanted == GROUP_WANTED)
        m_nextState = NNTP_XOVER_BEGIN;
	else if(m_typeWanted == NEW_GROUPS)
		m_nextState = NNTP_NEWGROUPS_BEGIN;
    else if(m_typeWanted == ARTICLE_WANTED ||
			m_typeWanted== CANCEL_WANTED)
	    m_nextState = NNTP_BEGIN_ARTICLE;
	else if (m_typeWanted== SEARCH_WANTED)
		m_nextState = NNTP_XPAT_SEND;
	else if (m_typeWanted == PRETTY_NAMES_WANTED)
		m_nextState = NNTP_LIST_PRETTY_NAMES;
#ifdef UNREADY_CODE
	else if (m_typeWanted == PROFILE_WANTED)
	{
		if (PL_strstr(ce->URL_s->address, "PROFILE NEW"))
			m_nextState = NNTP_PROFILE_ADD;
		else
			m_nextState = NNTP_PROFILE_DELETE;
	}
#endif
	else
	  {
		PR_ASSERT(0);
		return -1;
	  }

   return(0); /* good */
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////
// The following method is used for processing the news state machine. 
// It returns a negative number (mscott: we'll change this to be an enumerated type which we'll coordinate
// with the netlib folks?) when we are done processing.
//////////////////////////////////////////////////////////////////////////////////////////////////////////
nsresult nsNNTPProtocol::ProcessProtocolState(nsIURL * url, nsIInputStream * inputStream, PRUint32 length)
{
	PRInt32 status = 0; 

#ifdef UNREADY_CODE
    if (m_offlineNewsState != NULL)
	{
		return NET_ProcessOfflineNews(ce, cd);
	}
#endif
    
	ClearFlag(NNTP_PAUSE_FOR_READ); 

    while(!TestFlag(NNTP_PAUSE_FOR_READ))
	{

#if DEBUG_NEWS
        printf("Next state: %s\n",stateLabels[m_nextState]); 
#endif
		// examine our current state and call an appropriate handler for that state.....
        switch(m_nextState)
        {
            case NNTP_RESPONSE:
				if (inputStream == nsnull)
					SetFlag(NNTP_PAUSE_FOR_READ);
				else
					status = NewsResponse(inputStream, length);
                break;

			// mscott: I've removed the states involving connections on the assumption
			// that core netlib will now be managing that information.
			HG42871

            case NNTP_LOGIN_RESPONSE:
				if (inputStream == nsnull)
					SetFlag(NNTP_PAUSE_FOR_READ);
				else
					status = LoginResponse();
                break;

			case NNTP_SEND_MODE_READER:
                status = SendModeReader(); 
                break;

			case NNTP_SEND_MODE_READER_RESPONSE:
				if (inputStream == nsnull)
					SetFlag(NNTP_PAUSE_FOR_READ);
				else
					status = SendModeReaderResponse(); 
                break;

			case SEND_LIST_EXTENSIONS:
				status = SendListExtensions(); 
				break;
			case SEND_LIST_EXTENSIONS_RESPONSE:
				if (inputStream == nsnull)
					SetFlag(NNTP_PAUSE_FOR_READ);
				else
					status = SendListExtensionsResponse(inputStream, length);
				break;
			case SEND_LIST_SEARCHES:
				status = SendListSearches(); 
				break;
			case SEND_LIST_SEARCHES_RESPONSE:
				if (inputStream == nsnull) 
					SetFlag(NNTP_PAUSE_FOR_READ);
				else
					status = SendListSearchesResponse(inputStream, length); 
				break;
			case NNTP_LIST_SEARCH_HEADERS:
				status = SendListSearchHeaders();
				break;
			case NNTP_LIST_SEARCH_HEADERS_RESPONSE:
				if (inputStream == nsnull)
					SetFlag(NNTP_PAUSE_FOR_READ);
				else
					status = SendListSearchHeadersResponse(inputStream, length); 
				break;
			case NNTP_GET_PROPERTIES:
				status = GetProperties();
				break;
			case NNTP_GET_PROPERTIES_RESPONSE:
				if (inputStream == nsnull)
					SetFlag(NNTP_PAUSE_FOR_READ);
				else
					status = GetPropertiesResponse(inputStream, length);
				break;				
			case SEND_LIST_SUBSCRIPTIONS:
				status = SendListSubscriptions();
				break;
			case SEND_LIST_SUBSCRIPTIONS_RESPONSE:
				if (inputStream == nsnull)
					SetFlag(NNTP_PAUSE_FOR_READ);
				else
					status = SendListSubscriptionsResponse(inputStream, length);
				break;

            case SEND_FIRST_NNTP_COMMAND:
                status = SendFirstNNTPCommand(url);
                break;
            case SEND_FIRST_NNTP_COMMAND_RESPONSE:
				if (inputStream == nsnull)
					SetFlag(NNTP_PAUSE_FOR_READ);
				else
					status = SendFirstNNTPCommandResponse();
                break;

            case NNTP_SEND_GROUP_FOR_ARTICLE:
                status = SendGroupForArticle();
                break;
            case NNTP_SEND_GROUP_FOR_ARTICLE_RESPONSE:
				if (inputStream == nsnull)
					SetFlag(NNTP_PAUSE_FOR_READ);
				else
					status = SendGroupForArticleResponse();
                break;
            case NNTP_SEND_ARTICLE_NUMBER:
                status = SendArticleNumber();
                break;

            case SETUP_NEWS_STREAM:
                status = SetupForTransfer();
                break;

			case NNTP_BEGIN_AUTHORIZE:
				status = BeginAuthorization(); 
                break;

			case NNTP_AUTHORIZE_RESPONSE:
				if (inputStream == nsnull)
					SetFlag(NNTP_PAUSE_FOR_READ);
				else
					status = AuthorizationResponse(); 
                break;

			case NNTP_PASSWORD_RESPONSE:
				if (inputStream == nsnull)
					SetFlag(NNTP_PAUSE_FOR_READ);
				else
					status = PasswordResponse();
                break;
    
			// read list
            case NNTP_READ_LIST_BEGIN:
                status = BeginReadNewsList(); 
                break;
            case NNTP_READ_LIST:
                status = ReadNewsList(inputStream, length);
                break;

			// news group
			case DISPLAY_NEWSGROUPS:
				status = DisplayNewsgroups(); 
                break;
			case NNTP_NEWGROUPS_BEGIN:
				status = BeginNewsgroups();
                break;
   			case NNTP_NEWGROUPS:
				status = ProcessNewsgroups(inputStream, length);
                break;
    
			// article specific
            case NNTP_BEGIN_ARTICLE:
				status = BeginArticle(); 
                break;

            case NNTP_READ_ARTICLE:
				status = ReadArticle(inputStream, length);
				break;
			
            case NNTP_XOVER_BEGIN:
#ifdef UNREADY_CODE
			    NET_Progress(ce->window_id, XP_GetString(XP_PROGRESS_READ_NEWSGROUPINFO));
#endif
			    status = BeginReadXover();
                break;

			case NNTP_FIGURE_NEXT_CHUNK:
				status = FigureNextChunk(); 
				break;

			case NNTP_XOVER_SEND:
				status = XoverSend();
				break;
    
            case NNTP_XOVER:
                status = ReadXover(inputStream, length);
                break;

            case NNTP_XOVER_RESPONSE:
				if (inputStream == nsnull)
					SetFlag(NNTP_PAUSE_FOR_READ);
				else
					status = ReadXoverResponse();
                break;

            case NEWS_PROCESS_XOVER:
		    case NEWS_PROCESS_BODIES:
#ifdef UNREADY_CODE
                NET_Progress(ce->window_id, XP_GetString(XP_PROGRESS_SORT_ARTICLES));
#endif
				status = ProcessXover();
                break;

            case NNTP_READ_GROUP:
                status = ReadNewsgroup();
                break;
    
            case NNTP_READ_GROUP_RESPONSE:
				if (inputStream == nsnull)
					SetFlag(NNTP_PAUSE_FOR_READ);
				else
					status = ReadNewsgroupResponse();
                break;

            case NNTP_READ_GROUP_BODY:
                status = ReadNewsgroupResponse();
                break;

	        case NNTP_SEND_POST_DATA:
	            status = PostData();
	            break;
	        case NNTP_SEND_POST_DATA_RESPONSE:
				if (inputStream == nsnull)
					SetFlag(NNTP_PAUSE_FOR_READ);
				else
					status = PostDataResponse();
	            break;

			case NNTP_CHECK_FOR_MESSAGE:
				status = CheckForArticle();
				break;

			case NEWS_NEWS_RC_POST:
#ifdef UNREADY_CODE
		        status = net_NewsRCProcessPost(ce);
#endif
		        break;

            case NEWS_DISPLAY_NEWS_RC:
		        status = DisplayNewsRC(); 
				break;
            case NEWS_DISPLAY_NEWS_RC_RESPONSE:
				if (inputStream == nsnull)
					SetFlag(NNTP_PAUSE_FOR_READ);
				else
					status = DisplayNewsRCResponse();
		        break;

			// cancel
            case NEWS_START_CANCEL:
		        status = StartCancel();
		        break;

            case NEWS_DO_CANCEL:
		        status = Cancel();
		        break;

			// XPAT
			case NNTP_XPAT_SEND:
				status = XPATSend();
				break;
			case NNTP_XPAT_RESPONSE:
				if (inputStream == nsnull)
					SetFlag(NNTP_PAUSE_FOR_READ);
				else
					status = XPATResponse(inputStream, length);
				break;

			// search
			case NNTP_SEARCH:
				status = Search();
				break;
			case NNTP_SEARCH_RESPONSE:
				if (inputStream == nsnull)
					SetFlag(NNTP_PAUSE_FOR_READ);
				else
					status = SearchResponse();
				break;
			case NNTP_SEARCH_RESULTS:
				status = SearchResults(inputStream, length);
				break;

			
			case NNTP_LIST_PRETTY_NAMES:
				status = ListPrettyNames();
				break;
			case NNTP_LIST_PRETTY_NAMES_RESPONSE:
				if (inputStream == nsnull)
					SetFlag(NNTP_PAUSE_FOR_READ);
				else
					status = ListPrettyNamesResponse(inputStream, length);
				break;
			case NNTP_LIST_XACTIVE:
				status = ListXActive();
				break;
			case NNTP_LIST_XACTIVE_RESPONSE:
				if (inputStream == nsnull)
					SetFlag(NNTP_PAUSE_FOR_READ);
				else
					status = ListXActiveResponse(inputStream, length);
				break;
			case NNTP_LIST_GROUP:
				status = ListGroup();
				break;
			case NNTP_LIST_GROUP_RESPONSE:
				if (inputStream == nsnull)
					SetFlag(NNTP_PAUSE_FOR_READ);
				else
					status = ListGroupResponse(inputStream, length);
				break;
	        case NEWS_DONE:
			  /* call into libmsg and see if the article counts
			   * are up to date.  If they are not then we
			   * want to do a "news://host/group" URL so that we
			   * can finish up the article counts.

			   */
				m_nextState = NEWS_FREE;
#if 0   // mscott 01/04/99. This should be temporary until I figure out what to do with this code.....
			  if (cd->stream)
				COMPLETE_STREAM;

	            cd->next_state = NEWS_FREE;
                /* set the connection unbusy
     	         */
    		    cd->control_con->busy = PR_FALSE;
                NET_TotalNumberOfOpenConnections--;

				NET_ClearReadSelect(ce->window_id, cd->control_con->csock);
				NET_RefreshCacheFileExpiration(ce->URL_s);
#endif
	            break;

			case NEWS_POST_DONE:
#ifdef DEBUG_NEWS
				printf("NEWS_POST_DONE!\n");
#endif
				m_runningURL->SetUrlState(PR_FALSE, NS_OK);
				m_nextState = NEWS_FREE;
				break;
	        case NEWS_ERROR:
#ifdef DEBUG_NEWS
				printf("NEWS_ERROR!\n");
#endif
				m_runningURL->SetUrlState(PR_FALSE, NS_ERROR_FAILURE);
				m_nextState = NEWS_FREE;
#if 0   // mscott 01/04/99. This should be temporary until I figure out what to do with this code.....
	            if(cd->stream)
		             ABORT_STREAM(status);

    	        /* set the connection unbusy
     	         */
    		    cd->control_con->busy = PR_FALSE;
                NET_TotalNumberOfOpenConnections--;

				if(cd->control_con->csock != NULL)
				  {
					NET_ClearReadSelect(ce->window_id, cd->control_con->csock);
				  }
#endif
	            break;

	        case NNTP_ERROR:
#ifdef DEBUG_NEWS
				printf("NNTP_ERROR!\n");
#endif
#if 0   // mscott 01/04/99. This should be temporary until I figure out what to do with this code.....
	            if(cd->stream)
				  {
		            ABORT_STREAM(status);
					cd->stream=0;
				  }
    
				if(cd->control_con && cd->control_con->csock != NULL)
				  {
					NNTP_LOG_NOTE(("Clearing read and connect select on socket %d",
															cd->control_con->csock));
					NET_ClearConnectSelect(ce->window_id, cd->control_con->csock);
					NET_ClearReadSelect(ce->window_id, cd->control_con->csock);
#ifdef XP_WIN
					if(cd->calling_netlib_all_the_time)
					{
						cd->calling_netlib_all_the_time = PR_FALSE;
						NET_ClearCallNetlibAllTheTime(ce->window_id,"mknews");
					}
#endif /* XP_WIN */

#if defined(XP_WIN) || (defined(XP_UNIX)&&defined(UNIX_ASYNC_DNS))
                    NET_ClearDNSSelect(ce->window_id, cd->control_con->csock);
#endif /* XP_WIN || XP_UNIX */
				    net_nntp_close (cd->control_con, status);  /* close the
																  socket */ 
					NET_TotalNumberOfOpenConnections--;
					ce->socket = NULL;
				  }
#endif // mscott's temporary #if 0...
                /* check if this connection came from the cache or if it was
                 * a new connection.  If it was not new lets start it over
				 * again.  But only if we didn't have any successful protocol
				 * dialog at all.
                 */

				// mscott: I've removed the code that used to be here because it involved connection
				// management which should now be handled by the netlib module.
				m_nextState = NEWS_FREE;
				break;
    
            case NEWS_FREE:
				// mscott: we really haven't worked out how we are going to 
				// keep the news connection open. We probably want a news connection
				// cache so we aren't creating new connections to process each request...
				// but until that time, we always want to properly shutdown the connection

				// SendData(m_runningURL, "quit"CRLF); // this will cause OnStopBinding to get called
				if (m_transport)
					m_transport->OnStopBinding(m_runningURL, 0, nsnull);
				CloseSocket();
				return NS_OK;
				//SetFlag(NNTP_PAUSE_FOR_READ);
				break;

            default:
                /* big error */
                return NS_ERROR_FAILURE;
          
		} // end switch

        if(status < 0 && m_nextState != NEWS_ERROR &&
           m_nextState != NNTP_ERROR && m_nextState != NEWS_FREE)
        {
			m_nextState = NNTP_ERROR;
            ClearFlag(NNTP_PAUSE_FOR_READ);
        }
      
	} /* end big while */

	return NS_OK; /* keep going */
}

nsresult nsNNTPProtocol::CloseSocket()
{
  /* do we need to know if we're parsing xover to call finish xover?  */
  /* yes, I think we do! Why did I think we should??? */
  /* If we've gotten to NEWS_FREE and there is still XOVER
     data, there was an error or we were interrupted or
     something.  So, tell libmsg there was an abnormal
     exit so that it can free its data. */
            
	if (m_newsgroupList)
	{
		int status;
        nsresult rv;
       /* XXX - how/when to Release() this? */
        rv = m_newsgroupList->FinishXOVERLINE(status,&status);
		PR_ASSERT(NS_SUCCEEDED(rv));
		if (NS_SUCCEEDED(rv) && status >= 0 && status < 0)
					  status = status;
	}
	else
	{
      /* XXX - state is stored in the newshost - should
         we be releasing it here? */
      /* NS_RELEASE(m_newsgroup->GetNewsgroupList()); */
	}
#ifdef UNREADY_CODE
	if (cd->control_con)
		cd->control_con->last_used_time = PR_Now();
#endif

    PR_FREEIF(m_path);
    PR_FREEIF(m_responseText);
    PR_FREEIF(m_dataBuf);

	PR_FREEIF (m_cancelID);
	PR_FREEIF (m_cancelFromHdr);
	PR_FREEIF (m_cancelNewsgroups); 
	PR_FREEIF (m_cancelDistribution);

	m_runningURL = null_nsCOMPtr();

	return nsMsgProtocol::CloseSocket();
}
