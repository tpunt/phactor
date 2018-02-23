--TEST--
Ensure the serialisation points handle failure correctly.
--DESCRIPTION--
The serialisation points are:
 - ctor arguments when creating a new actor, and
 - message sending from an actor
--FILE--
<?php

use phactor\{ActorSystem, Actor, function spawn};

$actorSystem = new ActorSystem(true);

try {
    spawn('test', Test::class, fopen(__FILE__, 'r'));
} catch (Error $e) {
    var_dump($e->getMessage());
}

spawn('test2', Test2::class);

class Test extends Actor {
    public function receive($sender, $message) {}
}

class Test2 extends Actor
{
    public function __construct()
    {
        try {
            $this->send('test2', fopen(__FILE__, 'r'));
        } catch (Error $e) {
            var_dump($e->getMessage());
        }

        $this->send('test2', 1);
    }

    public function receive($sender, $message)
    {
        ActorSystem::shutdown();
    }
}
--EXPECT--
string(41) "Failed to serialise argument 2 of spawn()"
string(31) "Failed to serialise the message"
