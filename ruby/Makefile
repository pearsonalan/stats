.PHONY: all build test copy-lib-files

GEMVERSION = $(shell ruby -e 'require "./lib/stats/version.rb"; puts StatsGem::VERSION')
$(info gem version is $(GEMVERSION))

all: build test

build: copy-lib-files
	gem build fast-stats.gemspec
	gem install fast-stats-$(GEMVERSION).gem

copy-lib-files:
	if [ ! -d ext/stats/stats ]; then mkdir ext/stats/stats; fi
	cp ../include/stats/*.h ext/stats/stats
	cp ../src/*.c ext/stats
	cp ../ext/*.c ext/stats
	cp ../ext/*.h ext/stats

test:
	ruby ./test/test_stats.rb

benchmark:
	ruby ./test/benchmark_stats.rb
