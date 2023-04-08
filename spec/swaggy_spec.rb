# frozen_string_literal: true

RSpec.describe Swaggy do
  it "has a version number" do
    expect(Swaggy::VERSION).not_to be nil
  end

  it "says hello" do
    expect(Swaggy::Rack.hello).to eq "Hello from the C extension!"
  end

  it "does something useful" do
    expect(false).to eq(true)
  end
end
