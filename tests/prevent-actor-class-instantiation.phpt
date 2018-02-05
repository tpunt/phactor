--TEST--
Prevent actors from being instantiated via `new`
--DESCRIPTION--
This is not safe to do, and so it is prevented.
--FILE--
<?php

$actorSystem = new ActorSystem();

try {
    new class extends Actor {public function receive($sender, $message){}};
} catch (Error $e) {
    echo $e->getMessage();
}

$actorSystem->block();
--EXPECTF--
Actors cannot be created via class instantiation - please use spawn() instead
