--TEST--
Ensure correct copying of argument information.
--FILE--
<?php

use phactor\{ActorSystem, Actor, ActorRef};

$actorSystem = new ActorSystem();

class Test extends Actor
{
    public function receive() {ActorSystem::shutdown();}
    public function a(string $a, bool ...$b) : void {}
}

new ActorRef(Test::class, [], 'test');
--EXPECT--
