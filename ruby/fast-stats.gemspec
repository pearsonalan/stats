require './lib/stats/version'

Gem::Specification.new do |s|
  s.name = "fast-stats"
  s.version = StatsGem::VERSION
  s.authors = ["Alan Pearson"]
  s.date = Time.now.utc.strftime("%Y-%m-%d")
  s.extensions = ["ext/stats/extconf.rb"]
  s.files = `git ls-files .`.split("\n")
  s.require_paths = ["lib"]
  s.test_files = `git ls-files spec examples`.split("\n")
  s.required_ruby_version = ">= 1.9.3"
  s.summary = "stats monitoring"
end

