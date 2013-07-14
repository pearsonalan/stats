#!/usr/bin/env /usr/local/bin/ruby

require 'stats'
require 'benchmark'
require 'ruby-statsd'

puts "version is #{Stats.version}"

puts "creating stats object"
s = Stats.new("rubytest")

result = 0
times = Benchmark.measure do
  1_000_000.times do |n|
    # s.inc("foo")
    Statsd.increment("foo")
    result = result + (n+1)
  end
end

puts "Result is #{result}. time = #{times}"
