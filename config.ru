require_relative "lib/swaggy.rb"

run Swaggy::Rack.new('./test_openapi.json')
