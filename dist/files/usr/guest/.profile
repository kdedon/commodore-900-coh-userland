: 'guest HOME/.profile.  This shell has no comment lexer, so every remark'
: 'here is a : argument, the same way /.profile and /etc/rc are written.'
: ''
: 'login(1) has already set PATH=:/bin:/usr/bin, USER, HOME and SHELL for a'
: 'non-root account (cmd/login/login.c defenvn).  /etc is deliberately NOT'
: 'added: nothing under it is meant to be run by an ordinary user, and a'
: 'guest whose PATH reaches it finds shutdown, halt and umount.all.'
: ''
: 'TERM is set in /etc/profile, which every login shell reads before this'
: 'file.  A second copy of that logic here would only drift from it.'
: 'A guest reaches its own mail with mail(1); MAIL is what mail(1) and the'
: 'shell read to find the box.'
MAIL=/usr/spool/mail/guest
export MAIL
