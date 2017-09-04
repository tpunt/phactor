--TEST--
Ensure actors are properly cleaned up on shutdown
--DESCRIPTION--
All actors pertaining to its thread should be correctly freed, including all
actors in the main PHP thread.
--FILE--
<?php

$actorSystem = new ActorSystem();

class Test extends Actor
{
    public function receive($sender, $message)
    {
        new class extends Actor {public function receive($s, $m){}};
    }
}

new class(new Test) extends Actor {
    public function __construct(Actor $actor)
    {
        new class extends Actor {public function receive($s, $m){}};
        $this->send($actor, 1);
    }

    public function receive($sender, $message)
    {
        new class extends Actor {public function receive($s, $m){}};
        var_dump($sender);
    }
};

$actorSystem->block();

--EXPECTF--
