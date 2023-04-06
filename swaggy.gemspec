# frozen_string_literal: true

require_relative "lib/swaggy/version"

Gem::Specification.new do |spec|
  spec.name = "swaggy"
  spec.version = Swaggy::VERSION
  spec.authors = ["Nigel Baillie"]
  spec.email = ["nigel@baillie.dev"]

  spec.summary = "Mount an OpenAPI spec as a Rack app"
  spec.description = "Zero boilerplate server from your OpenAPI spec."
  spec.homepage = "https://github.com/Resonious/swaggy"
  spec.license = "MIT"
  spec.required_ruby_version = ">= 3.0.4"

  spec.metadata["allowed_push_host"] = "https://rubygems.org"

  spec.metadata["homepage_uri"] = spec.homepage
  spec.metadata["source_code_uri"] = "https://github.com/Resonious/swaggy"

  # Specify which files should be added to the gem when it is released.
  # The `git ls-files -z` loads the files in the RubyGem that have been added into git.
  spec.files = Dir.chdir(File.expand_path(__dir__)) do
    `git ls-files -z`.split("\x0").reject do |f|
      (f == __FILE__) || f.match(%r{\A(?:(?:test|spec|features)/|\.(?:git|travis|circleci)|appveyor)})
    end
  end
  spec.bindir = "exe"
  spec.executables = spec.files.grep(%r{\Aexe/}) { |f| File.basename(f) }
  spec.require_paths = ["lib"]

  # Uncomment to register a new dependency of your gem
  # spec.add_dependency "example-gem", "~> 1.0"

  # For more information and examples about making a new gem, checkout our
  # guide at: https://bundler.io/guides/creating_gem.html
end
