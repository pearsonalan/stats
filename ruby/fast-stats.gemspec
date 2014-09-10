require './lib/stats/version'

Gem::Specification.new do |s|
  s.name = "fast-stats"
  s.licenses = ["BSD-3"]
  s.version = StatsGem::VERSION
  s.authors = ["Alan Pearson"]
  s.homepage = "https://github.com/pearsonalan/stats"
  s.date = Time.now.utc.strftime("%Y-%m-%d")
  s.extensions = ["ext/stats/extconf.rb"]
  s.files = `git ls-files .`.split("\n")
  s.files += `find ext -name *.c`.split("\n")
  s.files += `find ext -name *.h`.split("\n")
  s.require_paths = ["lib"]
  s.test_files = `git ls-files spec examples`.split("\n")
  s.required_ruby_version = ">= 1.9.3"
  s.summary = "stats monitoring"
  s.description = "fast-stats is a SysV IPC based stats tracking tool. Counters are stored in shared memory, so updating or reading the counter is quite fast"
end

