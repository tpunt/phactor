--TEST--
Ensure correct copying of argument information.
--FILE--
<?php

use phactor\{ActorSystem, Actor, function spawn};

$actorSystem = new ActorSystem();

class Test extends Actor
{
    public function receive($sender, $message) {}
    public function a(string $a, bool ...$b) : void {}
}

spawn('test', Test::class);
--EXPECT--
