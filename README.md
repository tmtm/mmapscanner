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
* scan はポインタ位置で正規表現との一致を試みます。一致した部分を返し、ポインタを進めます。一致しない場合は nil を返します。
* scan_until は scan と同じですが、現在のポインタの位置以降で一致を試みます。
* check は scan と同じですが、ポインタを進めません。
* check_until は check と同じですが、現在のポインタの位置以降で一致を試みます。
* skip は scan と同じですが、一致したバイト数を返します。
* skip_until は skip と同じですが、現在のポインタの位置以降で一致を試みます。
* match? は check と同じですが、一致したバイト数を返します。
* exist? は match? と同じですが、現在のポインタの位置以降で一致を試みます。
* scan_full(re, s, f) はポインタの位置でスキャンします。
  * scan_full(re, true, true) は scan(re) と同じです。
  * scan_full(re, true, false) は skip(re) と同じです。
  * scan_full(re, false, true) は check(re) と同じです。
  * scan_full(re, false, false) は match?(re) と同じです。
* search_full(re, s, f) はポインタの位置以降でスキャンします。
  * search_full(re, true, true) は scan_until(re) と同じです。
  * search_full(re, true, false) は skip_until(re) と同じです。
  * search_full(re, false, true) は check_until(re) と同じです。
  * search_full(re, false, false) は exist?(re) と同じです。
* peek は指定したバイト数分のデータを返します。ポインタは進みません。
* eos? はポインタが末尾に達していると true を返します。
* rest はポインタ以降のデータを返します。
* matched は正規表現に一致した部分を MmapScanner オブジェクトで返します。
* matched(n) は正規表現の n番目の括弧に一致した部分を MmapScanner オブジェクトで返します。
* matched_str は matched と同じですが、文字列を返します。
* pos は現在のポインタの位置を返します。
* pos= でポインタ位置を変更することができます。
* terminate はポインタを末尾に移動します。

Copyright
---------

<dl>
<dt>Author<dd>TOMITA Masahiro <tommy@tmtm.org>
<dt>Copyrigh<dd>Copyright (c) 2011 TOMITA Masahiro
<dt>License<dd>Ruby's
</dl>

