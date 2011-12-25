require 'tempfile'

$LOAD_PATH.unshift "#{File.dirname __FILE__}/../ext"
require 'mmapscanner'

describe MmapScanner::Mmap do
  before do
    tmpf = Tempfile.new 'mmapscanner'
    tmpf.write '0123456789'*1000
    tmpf.flush
    @file = File.open(tmpf.path)
  end
  subject{MmapScanner::Mmap.new(@file)}
  it '#size returns file size' do
    subject.size.should == 10000
  end
  it '.new with invalid offset raise error' do
    expect{MmapScanner::Mmap.new(@file, 4095)}.to raise_error(Errno::EINVAL)
  end
  context 'unmapped object' do
    before do
      subject.unmap
    end
    it '#unmap raise error' do
      expect{subject.unmap}.to raise_error(RuntimeError, 'already unmapped')
    end
  end
end

describe MmapScanner do
  shared_examples_for 'MmapScanner' do
    it '#size returns size of file' do
      subject.size.should == 10000
    end
    it '#to_s returns contents of file' do
      subject.to_s.should == '0123456789'*1000
    end
    describe '#slice' do
      it 'returns MmapScanner' do
        subject.slice(5, 8).should be_instance_of MmapScanner
        subject.slice(5, 8).to_s.should == '56789012'
      end
    end
    it '#inspect returns "#<MmapScanner>"' do
      subject.inspect.should == '#<MmapScanner>'
    end
    it '#pos returns current position' do
      subject.pos.should == 0
      subject.scan(/.../)
      subject.pos.should == 3
    end
    describe '#pos=' do
      it 'change current position' do
        subject.pos = 100
        subject.pos.should == 100
      end
      it 'raise error when negative value' do
        expect{subject.pos = -1}.to raise_error(RangeError, 'out of range: -1')
      end
      it 'raise error when over size' do
        expect{subject.pos = 10001}.to raise_error(RangeError, 'out of range: 10001 > 10000')
        expect{subject.pos = 20000}.to raise_error(RangeError, 'out of range: 20000 > 10000')
      end
    end
    describe '#scan' do
      it 'returns matched data as MmapScanner' do
        ret = subject.scan(/\d{10}/)
        ret.class.should == MmapScanner
        ret.to_s.should == '0123456789'
      end
      it 'returns nil if not matched' do
        subject.scan(/123/).should be_nil
      end
      it 'forward current position' do
        subject.scan(/\d{10}/)
        subject.pos.should == 10
      end
    end
    describe '#scan_until' do
      it 'returns matched data as MmapScanner' do
        subject.scan(/012/)
        ret = subject.scan_until(/678/)
        ret.class.should == MmapScanner
        ret.to_s.should == '345678'
      end
      it 'returns nil if not matched' do
        subject.scan_until(/321/).should be_nil
      end
      it 'forward current position' do
        subject.scan_until(/456/)
        subject.pos.should == 7
      end
    end
    describe '#check' do
      it 'returns matched data as MmapScanner' do
        ret = subject.check(/\d{10}/)
        ret.class.should == MmapScanner
        ret.to_s.should == '0123456789'
      end
      it 'returns nil if not matched' do
        subject.check(/123/).should be_nil
      end
      it 'do not forward current position' do
        ret = subject.check(/\d{10}/)
        subject.pos.should == 0
      end
    end
    describe '#skip' do
      it 'returns length of matched data' do
        subject.skip(/\d{10}/).should == 10
      end
      it 'returns nil if not matched' do
        subject.skip(/123/).should be_nil
      end
      it 'forward current position' do
        subject.skip(/\d{10}/)
        subject.pos.should == 10
      end
    end
    describe '#match?' do
      it 'returns length of matched data' do
        subject.match?(/\d{10}/).should == 10
      end
      it 'returns nil if not matched' do
        subject.match?(/123/).should be_nil
      end
      it 'do not forward current position' do
        subject.match?(/\d{10}/)
        subject.pos.should == 0
      end
    end
    describe '#matched' do
      it 'returns matched data after scan' do
        subject.scan(/\d{6}/)
        subject.matched.to_s.should == '012345'
      end
      it 'returns matched data after scan_until' do
        subject.scan_until(/4567/)
        subject.matched.to_s.should == '4567'
      end
      it 'returns nil if there is not matched data' do
        subject.matched.should be_nil
      end
    end
    describe '#matched(nth)' do
      it 'returns nth part of matched string' do
        subject.scan(/(..)((aa)|..)(..)/)
        subject.matched(0).to_s.should == '012345'
        subject.matched(1).to_s.should == '01'
        subject.matched(2).to_s.should == '23'
        subject.matched(3).should be_nil
        subject.matched(4).to_s.should == '45'
        subject.matched(5).should be_nil
        subject.matched(-1).should be_nil
      end
    end
    describe '#peek' do
      it 'returns MmapScanner' do
        subject.peek(10).should be_instance_of MmapScanner
      end
      it 'do not forward current position' do
        subject.peek(10)
        subject.pos.should == 0
      end
    end
    describe '#eos?' do
      it 'returns true if eos' do
        subject.pos = 10000
        subject.eos?.should == true
      end
      it 'returns false if not eos' do
        subject.pos = 9999
        subject.eos?.should == false
      end
    end
    describe '#rest' do
      it 'returns rest data as MmapScanner' do
        subject.pos = 9997
        ret = subject.rest
        ret.should be_instance_of MmapScanner
        ret.to_s.should == '789'
      end
      it 'returns empty MmapScanner if it reached to end' do
        subject.pos = 10000
        subject.rest.to_s.should == ''
      end
    end
    describe '#terminate' do
      it 'set position to end of MmapScanner area' do
        subject.terminate.should == subject
        subject.pos.should == 10000
      end
    end
    describe '.new with position' do
      it '#size is length of rest data' do
        if src.respond_to? :size
          MmapScanner.new(src, 4096).size.should == src.size-4096
        else
          MmapScanner.new(src, 4096).size.should == File.size(src.path)-4096
        end
      end
    end
    describe '.new with length' do
      subject{MmapScanner.new(src, nil, 10)}
      it '#size is specified size' do
        subject.size.should == 10
      end
      it 'raise error when negative' do
        expect{MmapScanner.new(src, nil, -1)}.to raise_error(RangeError, 'length out of range: -1')
      end
    end
  end

  context 'with File' do
    before do
      tmpf = Tempfile.new 'mmapscanner'
      tmpf.write '0123456789'*1000
      tmpf.flush
      @file = File.open(tmpf.path)
    end
    let(:src){@file}
    subject{MmapScanner.new(src)}
    it_should_behave_like 'MmapScanner'
    describe '#data' do
      it 'returns Mmap object' do
        subject.data.should be_kind_of MmapScanner::Mmap
      end
    end
  end

  context 'with String' do
    let(:src){'0123456789'*1020}
    subject{MmapScanner.new(src, 100, 10000)}
    it_should_behave_like 'MmapScanner'
    describe '.new with empty source' do
      it 'returns empty MmapScanner' do
        m = MmapScanner.new('')
        m.size.should == 0
        m.to_s.should be_empty
      end
      it '#scan(//) returns empty MmapScanner' do
        m = MmapScanner.new('').scan(//)
        m.size.should == 0
      end
    end
    describe '#data' do
      it 'returns source object' do
        subject.data.should be_equal src
      end
    end
  end

  context 'with Mmap' do
    before do
      tmpf = Tempfile.new 'mmapscanner'
      tmpf.write '0123456789'*1020
      tmpf.flush
      @file = File.open(tmpf.path)
    end
    let(:src){MmapScanner::Mmap.new(@file)}
    subject{MmapScanner.new(src, 100, 10000)}
    it_should_behave_like 'MmapScanner'
    describe '.new with empty source' do
      it 'returns empty MmapScanner' do
        m = MmapScanner.new(src, 1020, 0)
        m.size.should == 0
        m.to_s.should be_empty
      end
      it '#scan(//) returns empty MmapScanner' do
        m = MmapScanner.new(src, 1020, 0).scan(//)
        m.size.should == 0
      end
    end
    describe '#data' do
      it 'returns source object' do
        subject.data.should be_equal src
      end
    end
    context 'unmapped object' do
      before do
        subject.scan(/.../)
        src.unmap
      end
      it '#to_s raise RuntimeError' do
        expect{subject.to_s}.to raise_error(RuntimeError, 'already unmapped')
      end
      it '#scan raise RuntimeError' do
        expect{subject.scan(/./)}.to raise_error(RuntimeError, 'already unmapped')
      end
      it '#matched_str raise RuntimeError' do
        expect{subject.matched_str}.to raise_error(RuntimeError, 'already unmapped')
      end
    end
  end

  context 'with MmapScanner' do
    before do
      tmpf = Tempfile.new 'mmapscanner'
      tmpf.write '0123456789'*1020
      tmpf.flush
      @file = File.open(tmpf.path)
    end
    let(:src){MmapScanner.new(@file)}
    subject{MmapScanner.new(src, 100, 10000)}
    it_should_behave_like 'MmapScanner'
    describe '#data' do
      it 'returns data object' do
        subject.data.should be_kind_of MmapScanner::Mmap
      end
    end
    describe '.new with empty source' do
      it 'returns empty MmapScanner' do
        m = MmapScanner.new(src, 1020, 0)
        m.size.should == 0
        m.to_s.should be_empty
      end
      it '#scan(//) returns empty MmapScanner' do
        m = MmapScanner.new(src, 1020, 0).scan(//)
        m.size.should == 0
      end
    end
  end
end
