Gem::Specification.new do |spec|
  spec.authors = 'TOMITA Masahiro'
  spec.email = 'tommy@tmtm.org'
  spec.extensions = ['ext/extconf.rb']
  spec.files = ['README.md', 'ext/mmapscanner.c']
  spec.homepage = 'http://github.com/tmtm/mmapscanner'
  spec.license = 'Ruby\'s'
  spec.name = 'mmapscanner'
  spec.required_ruby_version = '>= 1.9.2'
  spec.summary = 'MmapScanner like StringScanner but it use mmap(2)-ed data'
  spec.test_files = Dir.glob('spec/*_spec.rb')
  spec.version = '0.3'
end
