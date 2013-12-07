.PHONY: all build test

GEMVERSION = $(shell ruby -e 'require "./lib/stats/version.rb"; puts StatsGem::VERSION')
$(info gem version is $(GEMVERSION))

all: build
# all: test

build:
	gem build fast-stats.gemspec
	gem install fast-stats-$(GEMVERSION).gem

test:
	ruby ./test/test_stats.rb
