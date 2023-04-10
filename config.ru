require_relative "lib/swaggy.rb"

run Swaggy::Rack.new('../../r/hats/tmp/openapi.json')
