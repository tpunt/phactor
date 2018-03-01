--TEST--
Ensure message sending is correctly delayed if an actor is still being spawned.
--FILE--
<?php

use phactor\{ActorSystem, Actor, ActorRef};

$as = new ActorSystem();

class A extends Actor
{
    public function __construct()
    {
        new ActorRef(B::class, [], 'B');
        $this->send('B', 1);
    }

    public function receive(){}
}

class B extends Actor
{
    public function receive()
    {
        var_dump($this->receiveBlock());
        ActorSystem::shutdown();
    }
}

new ActorRef(A::class, [], 'A');
--EXPECT--
int(1)
