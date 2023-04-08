=begin

Here's maybe how we could structure a project:

project
  app
    models
    views
    controllers
    openapi
      komoju_v1.yml
  lib
  spec

Then, komoju_v1.yml gets loaded by this library and turned into controllers I guess?
And routes?

=end

# Alternatively, here's another idea. Simple library that isn't convention based
app = Swaggy::Rack.new('path/to/openapi.yml')
config.middleware.insert_before 0, app

