--TEST--
Ensure actors are properly cleaned up on shutdown
--DESCRIPTION--
All actors pertaining to its thread should be correctly freed, including all
actors in the main PHP thread.
--FILE--
<?php

use phactor\{ActorSystem, Actor, ActorRef};

$actorSystem = new ActorSystem();

class TestA extends Actor
{
    public function receive()
    {
        $this->receiveBlock();
        new ActorRef(TestA::class, [], 'testa3');
        ActorSystem::shutdown();
    }
}

new ActorRef(TestB::class, [], 'testb1');

class TestB extends Actor
{
    public function __construct()
    {
        new ActorRef(TestA::class, [], 'testa2');
        $this->send('testb1', 1);
    }

    public function receive()
    {
        $this->receiveBlock();
        $this->send('testa2', 1);
    }
}
--EXPECT--
