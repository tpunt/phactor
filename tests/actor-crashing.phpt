--TEST--
Actor crashing should not affect the rest of the actor system
--FILE--
<?php

use phactor\{ActorSystem, Actor, ActorRef};

$as = new ActorSystem(1);

class A extends Actor
{
    public function __construct()
    {
        throw new Exception();
    }

    public function receive(){}
}

class B extends Actor
{
    public function receive()
    {
        throw new Exception();
    }
}

class C extends Actor
{
    public function receive()
    {
        ActorSystem::shutdown();
    }
}

new ActorRef(A::class);
new ActorRef(B::class);
new ActorRef(C::class);
--EXPECT--
