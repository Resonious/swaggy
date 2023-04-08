# frozen_string_literal: true

RSpec.describe Swaggy do
  it "has a version number" do
    expect(Swaggy::VERSION).not_to be nil
  end

  it "says hello" do
    expect(Swaggy::Rack.hello).to eq "Hello from the C extension!"
  end

  it "loads an openapi JSON document" do
    rack = Swaggy::Rack.new("./test_openapi.json")
    expect(rack).to be_a Swaggy::Rack
  end
end
