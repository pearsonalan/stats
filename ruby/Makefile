.PHONY: all build test


all: build
# all: test

build:
	gem build stats.gemspec
	gem install stats-1.1.gem

test:
	ruby ./test/test_stats.rb
