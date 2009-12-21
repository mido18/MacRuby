require File.dirname(__FILE__) + "/../spec_helper"

describe "The Array class" do
  it "is an alias to NSMutableArray" do
    Array.should == NSMutableArray
  end

  it "can be subclassed and later instantiated" do
    k = Class.new(Array)
    a = k.new
    a.class.should == k
    a << 42
    a[0].should == 42
  end
end

describe "The NSArray class" do
  it "can be subclassed and later instantiated" do
    k = Class.new(NSArray)
    a = k.new
    a.class.should == k
    a.size.should == 0
    # TODO
    #lambda { a << 42 }.should raise_error(RuntimeError)
  end
end

describe "An Array object" do
  it "is an instance of the Array/NSMutableArray class" do
    [].class.should == Array
    [].kind_of?(Array).should == true
    [].instance_of?(Array).should == true
  end

  it "is mutable" do
    a = []
    a << 42
    a[0].should == 42
  end

  it "can have a singleton class" do
    a = []
    def a.foo; 42; end
    a.foo.should == 42
    a << 42
    a[0].should == 42
  end

  it "can have a singleton class with an attr_accessor" do
    a = []
    class << a
      attr_accessor :foo
    end
    a.foo = 42
    a.foo.should == 42
  end
end

describe "An NSArray object" do
  it "is an instance of the NSArray class" do
    a = NSArray.array
    a.class.should == NSArray
    a = NSArray.arrayWithObject(42)
    a.class.should == NSArray
  end

  it "is immutable" do
    a = NSArray.array
    a.size.should == 0
    lambda { a << 123 }.should raise_error(RuntimeError)
  end

=begin
  it "can have a singleton class" do
    a = NSArray.array
    def a.foo; 42; end
    a.foo.should == 42
    lambda { a << 123 }.should raise_error(RuntimeError)
  end
=end
end

describe "Objective-C Array methods" do
  before :each do
    @a = [1,2,3,4]
  end

  it "should be able to be aliased to other selectors" do
    class << @a
      alias :foo :count
    end

    @a.foo.should == @a.count
  end

  it "should be able to be aliased by pure Ruby methods" do
    class << @a
      def foo
        return 42
      end
      alias :count :foo
    end

    @a.count.should == 42
  end

  it "should be commutative when aliased" do
    class << @a
      def foo
        return 42
      end
      def do_alias
        alias :old_count :count
        alias :count :foo
      end
      def undo_alias
        alias :count :old_count
      end
    end

    @a.count.should == 4
    @a.do_alias
    @a.count.should == 42
    @a.undo_alias
    @a.count.should == 4
  end
end
