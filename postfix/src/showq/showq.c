/*++
/* NAME
/*	showq 8
/* SUMMARY
/*	list the Postfix mail queue
/* SYNOPSIS
/*	\fBshowq\fR [generic Postfix daemon options]
/* DESCRIPTION
/*	The \fBshowq\fR daemon reports the Postfix mail queue status.
/*	It is the program that emulates the sendmail `mailq' command.
/*
/*	The \fBshowq\fR daemon can also be run in stand-alone mode
/*	by the super-user. This mode of operation is used to emulate
/*	the `mailq' command while the Postfix mail system is down.
/* SECURITY
/* .ad
/* .fi
/*	The \fBshowq\fR daemon can run in a chroot jail at fixed low
/*	privilege, and takes no input from the client. Its service port
/*	is accessible to local untrusted users, so the service can be
/*	susceptible to denial of service attacks.
/* STANDARDS
/* .ad
/* .fi
/*	None. The showq daemon does not interact with the outside world.
/* DIAGNOSTICS
/*	Problems and transactions are logged to \fBsyslogd\fR(8).
/* BUGS
/*	The \fBshowq\fR daemon runs at a fixed low privilege; consequently,
/*	it cannot extract information from queue files in the
/*	\fBmaildrop\fR directory.
/* SEE ALSO
/*	cleanup(8) canonicalize and enqueue mail
/*	pickup(8) local mail pickup service
/*	qmgr(8) mail being delivered, delayed mail
/*	syslogd(8) system logging
/* LICENSE
/* .ad
/* .fi
/*	The Secure Mailer license must be distributed with this software.
/* AUTHOR(S)
/*	Wietse Venema
/*	IBM T.J. Watson Research
/*	P.O. Box 704
/*	Yorktown Heights, NY 10598, USA
/*--*/

/* System library. */

#include <sys_defs.h>
#include <sys/stat.h>
#include <dirent.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <string.h>
#include <ctype.h>

/* Utility library. */

#include <msg.h>
#include <scan_dir.h>
#include <vstring.h>
#include <vstream.h>
#include <vstring_vstream.h>
#include <stringops.h>
#include <mymalloc.h>
#include <htable.h>

/* Global library. */

#include <mail_queue.h>
#include <mail_open_ok.h>
#include <mail_proto.h>
#include <mail_date.h>
#include <mail_params.h>
#include <mail_scan_dir.h>
#include <mail_conf.h>
#include <record.h>
#include <rec_type.h>
#include <bounce_log.h>

/* Single-threaded server skeleton. */

#include <mail_server.h>

/* Application-specific. */

int     var_dup_filter_limit;

#define STRING_FORMAT	"%-10s %8s %-20s %s\n"
#define DATA_FORMAT	"%-10s%c%8ld %20.20s %s\n"

static void showq_reasons(VSTREAM *, BOUNCE_LOG *, HTABLE *);

static void showq_report(VSTREAM *client, char *queue, char *id,
			         VSTREAM *qfile, long size)
{
    VSTRING *buf = vstring_alloc(100);
    int     rec_type;
    time_t  arrival_time = 0;
    char   *start;
    long    msg_size = 0;
    BOUNCE_LOG *logfile;
    HTABLE *dup_filter = 0;
    char    status = (strcmp(queue, MAIL_QUEUE_ACTIVE) == 0 ? '*' : ' ');

    while (!vstream_ferror(client) && (rec_type = rec_get(qfile, buf, 0)) > 0) {
	start = vstring_str(buf);
	switch (rec_type) {
	case REC_TYPE_TIME:
	    arrival_time = atol(start);
	    break;
	case REC_TYPE_SIZE:
	    msg_size = atol(start);
	    break;
	case REC_TYPE_FROM:
	    if (*start == 0)
		start = "(MAILER-DAEMON)";
	    vstream_fprintf(client, DATA_FORMAT, id, status,
			  msg_size > 0 ? msg_size : size, arrival_time > 0 ?
			    asctime(localtime(&arrival_time)) : "??",
			    printable(start, '?'));
	    break;
	case REC_TYPE_RCPT:
	    if (*start == 0)			/* can't happen? */
		start = "(MAILER-DAEMON)";
	    if (dup_filter == 0 || htable_locate(dup_filter, start) == 0)
		vstream_fprintf(client, STRING_FORMAT,
				"", "", "", printable(start, '?'));
	    break;
	case REC_TYPE_MESG:
	    if (vstream_fseek(qfile, atol(start), SEEK_SET) < 0)
		msg_fatal("seek file %s: %m", VSTREAM_PATH(qfile));
	    break;
	case REC_TYPE_END:
	    break;
	}

	/*
	 * With the heading printed, see if there is a defer logfile. The
	 * defer logfile is not necessarily complete: delivery may be
	 * interrupted (postfix stop or reload) before all recipients have
	 * been tried.
	 * 
	 * Therefore we keep a record of recipients found in the defer logfile,
	 * and try to avoid listing those recipients again when processing
	 * the remainder of the queue file.
	 */
	if (rec_type == REC_TYPE_FROM
	    && dup_filter == 0
	    && (logfile = bounce_log_open(MAIL_QUEUE_DEFER, id, O_RDONLY, 0)) != 0) {
	    dup_filter = htable_create(var_dup_filter_limit);
	    showq_reasons(client, logfile, dup_filter);
	    if (bounce_log_close(logfile))
		msg_warn("close %s %s: %m", MAIL_QUEUE_DEFER, id);
	}
    }
    vstring_free(buf);
    if (dup_filter)
	htable_free(dup_filter, (void (*) (char *)) 0);
}

/* showq_reasons - show deferral reasons */

static void showq_reasons(VSTREAM *client, BOUNCE_LOG *bp, HTABLE *dup_filter)
{
    char   *saved_reason = 0;
    int     padding;

    while (bounce_log_read(bp) != 0) {

	/*
	 * Update the duplicate filter.
	 */
	if (var_dup_filter_limit == 0
	    || dup_filter->used < var_dup_filter_limit)
	    if (htable_locate(dup_filter, bp->recipient) == 0)
		htable_enter(dup_filter, bp->recipient, (char *) 0);

	/*
	 * Don't print the reason when the previous recipient had the same
	 * problem.
	 */
	if (saved_reason == 0 || strcmp(saved_reason, bp->text) != 0) {
	    if (saved_reason)
		myfree(saved_reason);
	    saved_reason = mystrdup(bp->text);
	    if ((padding = 76 - strlen(saved_reason)) < 0)
		padding = 0;
	    vstream_fprintf(client, "%*s(%s)\n", padding, "", saved_reason);
	}
	vstream_fprintf(client, STRING_FORMAT, "", "", "", bp->recipient);
    }
    if (saved_reason)
	myfree(saved_reason);
}


/* showq_service - service client */

static void showq_service(VSTREAM *client, char *unused_service, char **argv)
{
    char  **queue;
    VSTREAM *qfile;
    const char *path;
    int     status;
    char   *id;
    int     file_count;
    unsigned long queue_size = 0;
    struct stat st;
    char   *queue_names[] = {		/* XXX configurable */
	MAIL_QUEUE_INCOMING,
	MAIL_QUEUE_ACTIVE,
	MAIL_QUEUE_DEFERRED,
	/* No maildrop until we can disable recursive scans. */
	0,
    };

    /*
     * Sanity check. This service takes no command-line arguments.
     */
    if (argv[0])
	msg_fatal("unexpected command-line argument: %s", argv[0]);

    /*
     * Skip any files that have the wrong permissions. If we can't open an
     * existing file, assume the system is out of resources or that it is
     * mis-configured, and force backoff by raising a fatal error.
     */
    file_count = 0;
    for (queue = queue_names; *queue != 0; queue++) {
	SCAN_DIR *scan = scan_dir_open(*queue);
	char   *saved_id = 0;

	while ((id = mail_scan_dir_next(scan)) != 0) {

	    /*
	     * XXX I have seen showq loop on the same queue id. That would be
	     * an operating system bug, but who cares whose fault it is. Make
	     * sure this will never happen again.
	     */
	    if (saved_id) {
		if (strcmp(saved_id, id) == 0) {
		    msg_warn("readdir loop on queue %s id %s", *queue, id);
		    break;
		}
		myfree(saved_id);
	    }
	    saved_id = mystrdup(id);
	    status = mail_open_ok(*queue, id, &st, &path);
	    if (status == MAIL_OPEN_YES) {
		if (file_count == 0)
		    vstream_fprintf(client, STRING_FORMAT,
				    "-Queue ID-", "--Size--",
				    "----Arrival Time----",
				    "-Sender/Recipient-------");
		else
		    vstream_fprintf(client, "\n");
		if ((qfile = mail_queue_open(*queue, id, O_RDONLY, 0)) != 0) {
		    queue_size += st.st_size;
		    showq_report(client, *queue, id, qfile, (long) st.st_size);
		    if (vstream_fclose(qfile))
			msg_warn("close file %s %s: %m", *queue, id);
		} else if (strcmp(*queue, MAIL_QUEUE_MAILDROP) == 0) {
		    queue_size += st.st_size;
		    vstream_fprintf(client, DATA_FORMAT, id, ' ',
				    (long) st.st_size,
				    asctime(localtime(&st.st_mtime)),
				    "(to be determined)");
		} else if (errno != ENOENT)
		    msg_fatal("open %s %s: %m", *queue, id);
		file_count++;
	    }
	    vstream_fflush(client);
	}
	if (saved_id)
	    myfree(saved_id);
	scan_dir_close(scan);
    }
    if (file_count == 0)
	vstream_fprintf(client, "Mail queue is empty\n");
    else {
	vstream_fprintf(client, "\n-- %lu Kbytes in %d Request%s.\n",
			queue_size / 1024, file_count,
			file_count == 1 ? "" : "s");
    }
}

/* main - pass control to the single-threaded server skeleton */

int     main(int argc, char **argv)
{
    static CONFIG_INT_TABLE int_table[] = {
	VAR_DUP_FILTER_LIMIT, DEF_DUP_FILTER_LIMIT, &var_dup_filter_limit, 0, 0,
	0,
    };

    single_server_main(argc, argv, showq_service,
		       MAIL_SERVER_INT_TABLE, int_table,
		       0);
}