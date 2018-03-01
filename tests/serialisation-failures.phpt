--TEST--
Ensure the serialisation points handle failure correctly.
--DESCRIPTION--
The serialisation points are:
 - ctor arguments when creating a new actor, and
 - message sending from an actor
--FILE--
<?php

use phactor\{ActorSystem, Actor, ActorRef};

$actorSystem = new ActorSystem();

try {
    new ActorRef(Test::class, [fopen(__FILE__, 'r')]);
} catch (Error $e) {
    var_dump($e->getMessage());
}

new ActorRef(Test2::class, [], 'test2');

class Test extends Actor
{
    public function receive() {}
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

    public function receive()
    {
        $this->receiveBlock();
        ActorSystem::shutdown();
    }
}
--EXPECT--
string(45) "Failed to serialise the constructor arguments"
string(31) "Failed to serialise the message"
