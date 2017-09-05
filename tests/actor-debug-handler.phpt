--TEST--
Ensure actors are properly cleaned up on shutdown
--DESCRIPTION--
All actors pertaining to its thread should be correctly freed, including all
actors in the main PHP thread.
--FILE--
<?php

$actorSystem = new ActorSystem();

new class extends Actor {
    function __construct()
    {
        $this->send($this, 1);
    }

    function receive($sender, $message)
    {
        var_dump($this);
    }
};

$actorSystem->block();
--EXPECTF--
object(class@anonymous)#%d (0) {
}
