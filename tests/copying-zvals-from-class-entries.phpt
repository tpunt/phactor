--TEST--
Constant handling
--DESCRIPTION--
Ensure that constants are correctly handled.
--FILE--
<?php

$actorSystem = new ActorSystem(true);

register('test', Test::class);
register('test2', Test2::class);

class Test extends Actor {
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
        ActorSystem::shutdown();
    }
}

$actorSystem->block();
--EXPECT--
bool(true)
bool(false)
bool(true)
bool(false)
bool(true)
bool(false)
bool(true)
bool(false)
string(28) "Undefined class constant 'B'"
