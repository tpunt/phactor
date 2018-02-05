--TEST--
Ensure actors are properly cleaned up on shutdown
--DESCRIPTION--
All actors pertaining to its thread should be correctly freed, including all
actors in the main PHP thread.
--FILE--
<?php

$actorSystem = new ActorSystem(true);

class TestA extends Actor
{
    public function receive($sender, $message)
    {
        spawn('testa3', TestA::class);
        ActorSystem::shutdown();
    }
}

spawn('testa1', TestA::class);
spawn('testb1', TestB::class);

class TestB extends Actor
{
    public function __construct()
    {
        spawn('testa2', TestA::class);
        $this->send('testb1', 1);
    }

    public function receive($sender, $message)
    {
        spawn('testb2', TestA::class);
        $this->send('testa2', 1);
    }
}
--EXPECT--
