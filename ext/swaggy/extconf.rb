require 'mkmf'

libfyaml_source = File.expand_path('../../tmp/libfyaml-0.8', __dir__)
libfyaml_upstream = 'https://github.com/pantoniou/libfyaml/releases/download/v0.8/libfyaml-0.8.tar.gz'

if !File.exist?(libfyaml_source)
  puts "Downloading libfyaml..."
  `curl -L #{libfyaml_upstream} | tar xz -C #{File.dirname(libfyaml_source)}`
end

if !File.exist?(File.expand_path(libfyaml_source, 'build/lib/libfyaml.a'))
  puts "Building libfyaml from source"
  `mkdir -p "#{File.expand_path(libfyaml_source, 'build')}"`

  puts "Bootstrapping libfyaml..."
  `cd #{libfyaml_source} && ./bootstrap.sh`

  puts "Configuring libfyaml..."
  `cd #{libfyaml_source} && ./configure "--prefix=#{libfyaml_source}/build"`

  puts "Building libfyaml..."
  `cd #{libfyaml_source} && make install`

  puts "Forcing static link by removing libfyaml.so..."
  `rm #{libfyaml_source}/build/lib/libfyaml.so*`
end

$CFLAGS << " -I#{libfyaml_source}/build/include"
$LDFLAGS << " -L#{libfyaml_source}/build/lib -lfyaml"

extension_name = 'swaggy'
dir_config(extension_name)
create_makefile(extension_name)

