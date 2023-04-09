# frozen_string_literal: true

RSpec.describe Swaggy do
  it "has a version number" do
    expect(Swaggy::VERSION).not_to be nil
  end

  it "says hello" do
    expect(Swaggy::Rack.hello).to eq "Hello from the C extension!"
  end

  it "loads an openapi JSON document" do
    rack = Swaggy::Rack.new("./test_openapi.yaml")
    expect(rack).to be_a Swaggy::Rack

    env = {
      "REQUEST_METHOD" => "GET",
      "PATH_INFO" => "/customers/123/stuff",
      "QUERY_STRING" => "",
    }
    expect(rack.call(env)).to eq [
      200,
      {},
      ["Hello World"],
    ]
  end

  it "handles multiple slashes in request paths" do
    rack = Swaggy::Rack.new("./test_openapi.yaml")
    expect(rack).to be_a Swaggy::Rack

    env = {
      "REQUEST_METHOD" => "GET",
      "PATH_INFO" => "//customers///123//stuff",
      "QUERY_STRING" => "",
    }
    expect(rack.call(env)).to eq [
      200,
      {},
      ["Hello World"],
    ]
  end
end
