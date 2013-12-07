#!/usr/bin/env ruby

require 'stats'

puts "TEST STATS: version is #{Stats.version}"

s = Stats.new("rubytest")
ctr = s.get("ctr")
ctr.inc

puts "TEST STATS: OK"
