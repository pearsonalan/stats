#!/usr/bin/env ruby

require 'stats'
require 'benchmark'

puts "version is #{Stats.version}"

puts "creating stats object"
s = Stats.new("rubytest")

result = 0
times = Benchmark.measure do
  10_000_000.times do |n|
    result = result + n
  end
end

puts "Baseline with no counter: time = #{times}"

result = 0
times = Benchmark.measure do
  10_000_000.times do |n|
    s.inc("foo")
    result = result + n
  end
end

puts "Using Stats#inc: time = #{times}"

result = 0
ctr = s.get("bar")
times = Benchmark.measure do
  10_000_000.times do |n|
    ctr.inc
    result = result + n
  end
end

puts "Using Counter#inc:. time = #{times}"


result = 0
times = Benchmark.measure do
  10_000_000.times do |n|
    result = result + 1
  end
end

puts "Ruby variable increment: time = #{times}"

result = 0
ctr = s.get("bazz")
times = Benchmark.measure do
  10_000_000.times do |n|
    ctr.inc
  end
end

puts "Counter increment: time = #{times}"



result = 0
timer = s.timer("timer")
times = Benchmark.measure do
  10_000_000.times do |n|
    timer.enter
    result = result + n
    timer.exit
  end
end

puts "Timer: time = #{times}"



result = 0
timer = s.get("rtimer")
times = Benchmark.measure do
  10_000_000.times do |n|
    start = Time.now
    result = result + n
    timer.add(((Time.now.to_f - start.to_f) * 1000000).to_i)
  end
end

puts "Ruby Timer: time = #{times}"
