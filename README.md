# sshpass-debug

sshpass 1.05 with [optional] timeouts and [optional] debug output

-- nice utility from [http://sourceforge.net/projects/sshpass/][sshpass] , 
recommended for use with Ansible. However, sometimes it hangs, 
e.g.: [http://sourceforge.net/p/sshpass/bugs/7/][bug7] ,
and to debug that some syslog-based messages were added.

Current attempt is to add a timeout and see if it would be enough for our purposes 
to have just a few exceptions in the Ansible pool.

Note: the Makefile is a simple Linux-oriented one,
to restore it to a `./configure`-based build look at the history.

Usage: does not differ from original 'sshpass', 
except for a few environment variables are available:

    # log some data ( except for the password, of course ) with syslog
    export SSHPASS_DBG_SYSLOG=1
    # sets a (killing) timeout for every pty read,
    # so that long reads from the pseudo-terminal will terminate the application
    export SSHPASS_READ_TIMEOUT=10


[sshpass]: http://sourceforge.net/p/sshpass
[bug7]: http://sourceforge.net/p/sshpass/bugs/7/