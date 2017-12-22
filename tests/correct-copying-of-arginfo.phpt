--TEST--
Ensure correct copying of argument information.
--FILE--
<?php

$actorSystem = new ActorSystem();

class Test extends Actor
{
    public function receive($sender, $message) {}
    public function a(string $a, bool ...$b) : void {}
}

register('test', Test::class);

$actorSystem->block();
--EXPECT--
