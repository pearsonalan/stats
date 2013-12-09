require 'mkmf'
require 'rbconfig'

$CFLAGS << ' -Wall -Wno-multichar'
$CFLAGS << ' -Wextra -O0 -ggdb3' if ENV['DEBUG']

$CFLAGS << ' -DDEBUG=1' if ENV['DEBUG']
$CFLAGS << ' -DDEBUG=0' unless ENV['DEBUG']

$uname = `uname -a`
if /linux/i =~ $uname
  $CFLAGS << ' -DLINUX'
  have_library('rt','clock_gettime') && append_library($libs,'rt')
end

$CFLAGS << ' -DDARWIN' if /darwin/i =~ $uname

create_makefile('stats/stats')
