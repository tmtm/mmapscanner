MmapScanner
===========

Description
-----------

文字列の代わりにファイルを mmap(2) した領域に対して StringScanner のようなことをするものです。

Installation
------------

    $ cd ext
    $ ruby ./extconf.rb
    $ make
    $ sudo make install

Gem Installation
----------------

    $ gem install mmapscanner

Features
--------

* ファイルを mmap(2) した領域から正規表現に適合した部分データを返します。
* 返されるデータも MmapScanner オブジェクトで、最初にファイルから mmap(2) した領域を共有しています。
* mmap(2) を使用しているので大量データでもメモリを消費しません。to_s することではじめて String オブジェクトを生成します。

Usage
-----

* MmapScanner.new でファイルを mmap(2) します。mmap(2) できないファイルやパラメータを渡すとエラーになります。

        # ファイル全体を mmap
        ms = MmapScanner.new(File.open("filename"))
        # ファイルの先頭 4096 バイト以降を mmap
        ms = MmapScanner.new(File.open("filename"), 4096)
        # ファイルの先頭 4096 バイト以降の 1234 バイト分を mmap
        ms = MmapScanner.new(File.open("filename"), 4096, 1234)

* MmapScanner.new は文字列も受け付けます。文字列が途中で変更された時の動作は不定です。

* size, length は mmap(2) したサイズを返します。
* to_s は mmap(2) した領域を String で返します。Encoding は常に ASCII-8BIT です。
* slice は mmap(2) した領域の一部を新たな MmapScanner オブジェクトで返します。
* scan は正規表現に一致した部分を返し、ポインタを進めます。一致しない場合は nil を返します。
* check は scan と同じですが、ポインタを進めません。
* skip は scan と同じですが、一致したバイト数を返します。
* match? は check と同じですが、一致したバイト数を返します。
* peek は指定したバイト数分のデータを返します。ポインタは進みません。
* eos? はポインタが末尾に達していると true を返します。
* rest はポインタ以降のデータを返します。
* pos は現在のポインタの位置を返します。
* pos= でポインタ位置を変更することができます。

Copyright
---------

<dl>
<dt>Author<dd>TOMITA Masahiro <tommy@tmtm.org>
<dt>Copyrigh<dd>Copyright (c) 2011 TOMITA Masahiro
<dt>License<dd>Ruby's
</dl>

