#!/usr/bin/env ruby

require 'stats'

puts "version is #{Stats.version}"

puts "creating stats object"
s = Stats.new("rubytest")
ctr = s.get("ctr")
ctr.inc
