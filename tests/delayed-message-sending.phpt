--TEST--
Ensure message sending is correctly delayed if an actor is still being spawned.
--FILE--
<?php

use phactor\{ActorSystem, Actor, function spawn};

$as = new ActorSystem(true);

class A extends Actor
{
    public function __construct()
    {
        spawn('B', B::class);
        $this->send('B', 1);
    }

    public function receive($sender, $message){}
}

class B extends Actor
{
    public function receive($sender, $message)
    {
        var_dump($message);
        ActorSystem::shutdown();
    }
}

spawn('A', A::class);
--EXPECT--
int(1)
