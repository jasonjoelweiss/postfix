#!/bin/sh

# Sample Postfix installation script. Run this from the top-level
# Postfix source directory.

PATH=/bin:/usr/bin:/usr/sbin:/usr/etc:/sbin:/etc
umask 022

cat <<EOF

Warning: this script replaces existing sendmail or Postfix programs.
Make backups if you want to be able to recover.

In addition to doing a fresh install, this script can change an
existing installation from using a world-writable maildrop to a
group-writable one. It cannot be used to change Postfix queue
file/directory ownership.

Before installing files, this script prompts you for some definitions.
Most definitions will be remembered, so you have to specify them
only once. All definitions have a reasonable default value.

    install_root - prefix for installed file names (for package building)

    config_directory - directory with Postfix configuration files.
    daemon_directory - directory with Postfix daemon programs.
    command_directory - directory with Postfix administrative commands.
    queue_directory - directory with Postfix queues.

    sendmail_path - full pathname of the Postfix sendmail command.
    newaliases_path - full pathname of the Postfix newaliases command.
    mailq_path - full pathname of the Postfix mailq command.

    mail_owner - owner of Postfix queue files.

    setgid - groupname, e.g., postdrop (default: no). See INSTALL section 12.
    manpages - "no" or path to man tree. Example: /usr/local/man.

EOF

# By now, shells must have functions. Ultrix users must use sh5 or lose.

# Apparently, some broken LINUX file utilities won't move symlinks across
# file systems. Upgrade to a better system. Don't waste my time.

compare_or_replace() {
    cmp $2 $3 >/dev/null 2>&1 || {
	rm -f junk || exit 1
	cp $2 junk || exit 1
	mv -f junk $3 || exit 1
	chmod $1 $3 || exit 1
    }
}

compare_or_symlink() {
    cmp $1 $2 >/dev/null 2>&1 || {
	rm -f junk || exit 1
	ln -s $1 junk || exit 1
	mv -f junk $2 || exit 1
    }
}

compare_or_move() {
    cmp $2 $3 >/dev/null 2>&1 || {
	mv -f $2 $3 || exit 1
	chmod $1 $3 || exit 1
    }
}

# How to supress newlines in echo

case `echo -n` in
"") n=-n; c=;;
 *) n=; c='\c';;
esac

# Default settings. These are clobbered by remembered settings.

install_root=/
config_directory=/etc/postfix
daemon_directory=/usr/libexec/postfix
command_directory=/usr/sbin
queue_directory=/var/spool/postfix
sendmail_path=/usr/sbin/sendmail
newaliases_path=/usr/bin/newaliases
mailq_path=/usr/bin/mailq
mail_owner=postfix
setgid=no
manpages=/usr/local/man

# Find out the location of configuration files.

for name in install_root config_directory
do
    while :
    do
	eval echo \$n "$name: [\$$name]\  \$c"
	read ans
	case $ans in
	"") break;;
	 *) eval $name=\$ans; break;;
	esac
    done
done

# Sanity checks

for path in $install_root $config_directory
do
   case $path in
   /*) ;;
    *) echo "$path should be an absolute path name" 1>&2; exit 1;;
   esac
done

# In case some systems special-case pathnames beginning with //.

case $install_root in
/) install_root=
esac

# Load defaults from existing installation.

CONFIG_DIRECTORY=$install_root$config_directory

test -f $CONFIG_DIRECTORY/main.cf && {
    for name in daemon_directory command_directory queue_directory mail_owner 
    do
	eval $name='"`bin/postconf -c $CONFIG_DIRECTORY -h $name`"' || kill $$
    done
}

test -f $CONFIG_DIRECTORY/install.cf && . $CONFIG_DIRECTORY/install.cf

# Override default settings.

for name in daemon_directory command_directory \
    queue_directory sendmail_path newaliases_path mailq_path mail_owner\
    setgid manpages
do
    while :
    do
	eval echo \$n "$name: [\$$name]\  \$c"
	read ans
	case $ans in
	"") break;;
	 *) eval $name=\$ans; break;;
	esac
    done
done

# Sanity checks

for path in $daemon_directory $command_directory \
    $queue_directory $sendmail_path $newaliases_path $mailq_path $manpages
do
   case $path in
   /*) ;;
   no) ;;
    *) echo "$path should be an absolute path name" 1>&2; exit 1;;
   esac
done

rm -f junk || exit 1
touch junk

chown "$mail_owner" junk >/dev/null 2>&1 || {
    echo "Error: $mail_owner needs an entry in the passwd file" 1>&2
    echo "Remember, $mail_owner must have a dedicated user id and group id." 1>&2
    exit 1
}

case $setgid in
no) ;;
 *) chgrp "$setgid" junk >/dev/null 2>&1 || {
        echo "Error: $setgid needs an entry in the group file" 1>&2
        echo "Remember, $setgid must have a dedicated group id." 1>&2
        exit 1
    }
esac

rm -f junk

# Avoid clumsiness.

DAEMON_DIRECTORY=$install_root$daemon_directory
COMMAND_DIRECTORY=$install_root$command_directory
QUEUE_DIRECTORY=$install_root$queue_directory
SENDMAIL_PATH=$install_root$sendmail_path
NEWALIASES_PATH=$install_root$newaliases_path
MAILQ_PATH=$install_root$mailq_path
MANPAGES=$install_root$manpages

# Create any missing directories.

test -d $CONFIG_DIRECTORY || mkdir -p $CONFIG_DIRECTORY || exit 1
test -d $DAEMON_DIRECTORY || mkdir -p $DAEMON_DIRECTORY || exit 1
test -d $COMMAND_DIRECTORY || mkdir -p $COMMAND_DIRECTORY || exit 1
test -d $QUEUE_DIRECTORY || mkdir -p $QUEUE_DIRECTORY || exit 1
for path in $SENDMAIL_PATH $NEWALIASES_PATH $MAILQ_PATH
do
    dir=`echo $path|sed 's/[^/]*[/]*$//'`
    test -d $dir || mkdir -p $dir || exit 1
done

# Install files. Be careful to not copy over running programs.

for file in `ls libexec | grep -v '^\.'`
do
    compare_or_replace a+x,go-w libexec/$file $DAEMON_DIRECTORY/$file || exit 1
done

for file in `ls bin | grep '^post'`
do
    compare_or_replace a+x,go-w bin/$file $COMMAND_DIRECTORY/$file || exit 1
done

test -f bin/sendmail && {
    compare_or_replace a+x,go-w bin/sendmail $SENDMAIL_PATH || exit 1
    compare_or_symlink $sendmail_path $NEWALIASES_PATH
    compare_or_symlink $sendmail_path $MAILQ_PATH
}

compare_or_replace a+r,go-w conf/LICENSE $CONFIG_DIRECTORY/LICENSE || exit 1

test -f $CONFIG_DIRECTORY/main.cf || {
    cp conf/* $CONFIG_DIRECTORY || exit 1
    chmod a+r,go-w $CONFIG_DIRECTORY/* || exit 1

    test -z "$install_root" && {
	echo "Warning: you still need to edit myorigin/mydestination in" 1>&2
	echo "$CONFIG_DIRECTORY/main.cf. See also html/faq.html for dialup" 1>&2
	echo "sites or for sites inside a firewalled network." 1>&2
	echo "" 1>&2
	echo "BTW: Edit your alias database and be sure to set up aliases" 1>&2
	echo "for root and postmaster, then run $NEWALIASES_PATH." 1>&2
    }
}

# Save settings.

bin/postconf -c $CONFIG_DIRECTORY -e \
    "daemon_directory = $daemon_directory" \
    "command_directory = $command_directory" \
    "queue_directory = $queue_directory" \
    "mail_owner = $mail_owner" \
|| exit 1

(echo "# This file was generated by $0"
for name in sendmail_path newaliases_path mailq_path setgid manpages
do
    eval echo $name=\$$name
done) >junk || exit 1
compare_or_move a+x,go-w junk $CONFIG_DIRECTORY/install.cf || exit 1
rm -f junk

# Use set-gid privileges instead of writable maildrop (optional).

test -d $QUEUE_DIRECTORY/maildrop || {
    mkdir -p $QUEUE_DIRECTORY/maildrop || exit 1
    chown $mail_owner $QUEUE_DIRECTORY/maildrop || exit 1
}

case $setgid in
no)
    chmod 1733 $QUEUE_DIRECTORY/maildrop || exit 1
    chmod g-s $COMMAND_DIRECTORY/postdrop || exit 1
    postfix_script=conf/postfix-script-nosgid
    ;;
 *) 
    chgrp $setgid $COMMAND_DIRECTORY/postdrop || exit 1
    chmod g+s $COMMAND_DIRECTORY/postdrop || exit 1
    chgrp $setgid $QUEUE_DIRECTORY/maildrop || exit 1
    chmod 1730 $QUEUE_DIRECTORY/maildrop || exit 1
    postfix_script=conf/postfix-script-sgid
    ;;
esac

compare_or_replace a+x,go-w $postfix_script $CONFIG_DIRECTORY/postfix-script ||
    exit 1

# Install manual pages (optional).

case $manpages in
no) ;;
 *) (
     cd man || exit 1
     for dir in man?
	 do test -d $MANPAGES/$dir || mkdir -p $MANPAGES/$dir || exit 1
     done
     for file in man?/*
     do
	 cmp -s $file $MANPAGES/$file || {
	     rm -f $MANPAGES/$file
	     cp $file $MANPAGES/$file || exit 1
	     chmod 644 $MANPAGES/$file || exit 1
	 }
     done
    )
esac
