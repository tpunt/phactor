--TEST--
Testing that serialising objects in the context copying code fails
--FILE--
<?php

use phactor\ActorSystem;

class Test
{
    public $a;
    public static $b;

    public function __construct()
    {
        $this->a = new StdClass();
        self::$b = new StdClass();
    }
}

new Test();

$actorSystem = new ActorSystem();
--EXPECT--
