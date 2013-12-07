require 'mkmf'
require 'rbconfig'

$CFLAGS << ' -Wall -Wno-multichar'
$CFLAGS << ' -Wextra -O0 -ggdb3' if ENV['DEBUG']

$CFLAGS << ' -DDEBUG=1' if ENV['DEBUG']
$CFLAGS << ' -DDEBUG=0' unless ENV['DEBUG']

$CFLAGS << ' -DLINUX' if /linux/i =~ `uname -a`

create_makefile('stats/stats')
