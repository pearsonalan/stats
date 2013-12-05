.PHONY: all build test


all: build
# all: test

build:
	gem build fast-stats.gemspec
	gem install fast-stats-1.2.gem

test:
	ruby ./test/test_stats.rb
