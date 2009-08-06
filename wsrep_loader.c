/* Copyright (C) 2009 Codership Oy <info@codersihp.com>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*! @file wsrep implementation loader */

#include <dlfcn.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>

#include "wsrep_api.h"

// Logging stuff for the loader
static const char* log_levels[] = {"FATAL", "ERROR", "WARN", "INFO", "DEBUG"};

static void default_logger (wsrep_log_level_t lvl, const char* msg)
{
    fprintf (stderr, "wsrep loader: [%s] %s\n", log_levels[lvl], msg);
}

static wsrep_log_cb_t logger = default_logger;

/**************************************************************************
 * Library loader
 **************************************************************************/

static int verify(const wsrep_t *wh, const char *iface_ver)
{
    const size_t msg_len = 128;
    char msg[msg_len];

#define VERIFY(_p) if (!(_p)) {                                       \
	snprintf(msg, msg_len, "wsrep_load(): verify(): %s\n", # _p); \
        logger (WSREP_LOG_ERROR, msg);                                \
	return EINVAL;						      \
    }

    VERIFY(wh);
    VERIFY(wh->version);

    if (strcmp(wh->version, iface_ver)) {
        snprintf (msg, msg_len,
                  "WSREP interface version mismatch: required '%s', found '%s'",
                  iface_ver, wh->version);
        logger (WSREP_LOG_ERROR, msg);
        return EINVAL;
    }

    VERIFY(wh->init);
    VERIFY(wh->connect);
    VERIFY(wh->disconnect);
    VERIFY(wh->dbug_push);
    VERIFY(wh->dbug_pop);
    VERIFY(wh->recv);
    VERIFY(wh->commit);
    VERIFY(wh->replay_trx);
    VERIFY(wh->cancel_commit);
    VERIFY(wh->cancel_slave);
    VERIFY(wh->committed);
    VERIFY(wh->rolledback);
    VERIFY(wh->append_query);
    VERIFY(wh->append_row_key);
    VERIFY(wh->set_variable);
    VERIFY(wh->set_database);
    VERIFY(wh->to_execute_start);
    VERIFY(wh->to_execute_end);
    VERIFY(wh->sst_sent);
    VERIFY(wh->sst_received);
    return 0;
}


static wsrep_loader_fun wsrep_dlf(void *dlh, const char *sym)
{
    union {
	wsrep_loader_fun dlfun;
	void *obj;
    } alias;
    alias.obj = dlsym(dlh, sym);
    return alias.dlfun;
}

extern int wsrep_dummy_loader(wsrep_t *w);

int wsrep_load(const char *spec, wsrep_t **hptr, wsrep_log_cb_t log_cb)
{
    int ret = 0;
    void *dlh = NULL;
    wsrep_loader_fun dlfun;
    const size_t msg_len = 128;
    char msg[msg_len];

    if (NULL != log_cb)
        logger = log_cb;
    
    if (!(spec && hptr))
        return EINVAL;
    
    snprintf (msg, msg_len,
              "wsrep_load(): loading provider library '%s'", spec);
    logger (WSREP_LOG_INFO, msg);
    
    if (!(*hptr = malloc(sizeof(wsrep_t)))) {
	logger (WSREP_LOG_FATAL, "wsrep_load(): out of memory");
        return ENOMEM;
    }

    if (!spec || strcmp(spec, WSREP_NONE) == 0) {
        if ((ret = wsrep_dummy_loader(*hptr)) != 0) {
	    free (*hptr);
            *hptr = NULL;
        }
	return ret;
    }
    
    if (!(dlh = dlopen(spec, RTLD_NOW | RTLD_LOCAL))) {
	snprintf(msg, msg_len, "wsrep_load(): dlopen(): %s", dlerror());
        logger (WSREP_LOG_ERROR, msg);
        ret = EINVAL;
	goto out;
    }
    
    if (!(dlfun = wsrep_dlf(dlh, "wsrep_loader"))) {
        ret = EINVAL;
	goto out;
    }
    
    if ((ret = (*dlfun)(*hptr)) != 0) {
        snprintf(msg, msg_len, "wsrep_load(): loader failed: %s",
                 strerror(ret));
        logger (WSREP_LOG_ERROR, msg);
        goto out;
    }
    
    if ((ret = verify(*hptr, WSREP_INTERFACE_VERSION)) != 0 &&
        (*hptr)->free) {
	logger (WSREP_LOG_ERROR, "wsrep_load(): interface version mismatch.");
        (*hptr)->free(*hptr);
        goto out;
    }
    
    (*hptr)->dlh = dlh;

out:
    if (ret != 0) {
        if (dlh)
            dlclose(dlh);
        free(*hptr);
        *hptr = NULL;
    } else {
        logger (WSREP_LOG_INFO, "wsrep_load(): provider loaded succesfully.");
    }

    return ret;
}

void wsrep_unload(wsrep_t *hptr)
{
    if (!hptr) {
        logger (WSREP_LOG_WARN, "wsrep_unload(): null pointer.");
    } else {
        if (hptr->free)
            hptr->free(hptr);
        if (hptr->dlh)
            dlclose(hptr->dlh);
        free(hptr);
    }
}
