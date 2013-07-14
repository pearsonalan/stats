require 'mkmf'
require 'rbconfig'

$CFLAGS << ' -Wall'
$CFLAGS << ' -Wextra -O0 -ggdb3' if ENV['DEBUG']

find_header('stats/stats.h') or abort "Can't find the stats.h header"
have_library('stats','fast_hash') && append_library($libs,'stats')
create_makefile('stats/stats')
