#!/usr/bin/env ruby

require 'stats'

puts "TEST STATS: version is #{Stats.version}"

s = Stats.new("rubytest")
ctr = s.get("ctr")
ctr.clr
ctr.inc

raise "unexpected value" unless ctr.get == 1

d = s.sample

# puts "Sample is #{d.inspect}"

raise "unexpected sample count" unless d.count == 1
raise "unexpected sample keys" unless d.keys == ['ctr']
raise "unexpected sample value" unless d['ctr'] == 1

h = Hash.new
d.each do |k,v|
  h[k] = v
end

raise "unexpected sample data" unless h == {"ctr" => 1}

puts "TEST STATS: OK"
