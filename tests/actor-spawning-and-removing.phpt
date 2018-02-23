--TEST--
Spawning and removing actors
--FILE--
<?php

use phactor\{ActorSystem, Actor, function spawn, function remove};

ini_set('memory_limit', -1);

$as = new ActorSystem(true, 1);

class A extends Actor
{
    public function receive($sender, $message){}
}

class Shutdown extends Actor
{
    public function __construct()
    {
        $this->send($this, 1);
    }

    public function receive($sender, $message)
    {
        $this->remove();
        ActorSystem::shutdown();
    }
}

while (spawn('A', A::class) < 1000);

var_dump(remove('A', 100));
var_dump(remove('A'));
var_dump(remove('A'));

spawn('s', Shutdown::class);
--EXPECT--
int(100)
int(900)
int(0)
