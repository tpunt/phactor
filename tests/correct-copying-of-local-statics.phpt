--TEST--
Ensure correct copying of local static variables.
--FILE--
<?php

use phactor\{ActorSystem, Actor, function spawn};

const A = 1;

$actorSystem = new ActorSystem();

class Test extends Actor
{
    public function __construct()
    {
        static $a = A;
        static $b = A . A;
    }
    public function receive($sender, $message) {}
}

spawn('test', Test::class);
--EXPECT--
