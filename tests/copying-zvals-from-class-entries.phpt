--TEST--
Constant handling
--DESCRIPTION--
Ensure that constants are correctly handled.
--FILE--
<?php

use phactor\{ActorSystem, Actor, function spawn};

$actorSystem = new ActorSystem(true);

spawn('test', Test::class);
spawn('test2', Test2::class);

class Test extends Actor
{
    public function receive($sender, $message)
    {
        var_dump(Test2::E, Test2::F);
        $this->send($sender, 1);
    }
}

class Test2 extends Actor
{
    public $a = true, $b = false;
    public static $c = true, $d = false;
    const E = true, F = false;

    public const X = [
        __DIR__ . '/A',
        ['B' => 'c']
    ];

    public static $y = [
        __DIR__ . '/A',
        ['B' => 'c']
    ];

    public $z = [
        __DIR__ . '/A',
        ['B' => 'c']
    ];

    public function __construct()
    {
        $this->send('test', 1);
    }

    public function receive($sender, $message)
    {
        var_dump($this->a, $this->b, self::$c, static::$d, self::E, static::F);
        try {
            var_dump(Test2::B);
        } catch (Error $e) {
            var_dump($e->getMessage());
        }
        var_dump(self::X, self::$y, $this->z);
        ActorSystem::shutdown();
    }
}
--EXPECTF--
bool(true)
bool(false)
bool(true)
bool(false)
bool(true)
bool(false)
bool(true)
bool(false)
string(28) "Undefined class constant 'B'"
array(2) {
  [0]=>
  string(%d) "%s/A"
  [1]=>
  array(1) {
    ["B"]=>
    string(1) "c"
  }
}
array(2) {
  [0]=>
  string(%d) "%s/A"
  [1]=>
  array(1) {
    ["B"]=>
    string(1) "c"
  }
}
array(2) {
  [0]=>
  string(%d) "%s/A"
  [1]=>
  array(1) {
    ["B"]=>
    string(1) "c"
  }
}
