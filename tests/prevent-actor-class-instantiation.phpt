--TEST--
Prevent actors from being instantiated via `new`
--DESCRIPTION--
This is not safe to do, and so it is prevented.
--FILE--
<?php

use phactor\{ActorSystem, Actor};

$actorSystem = new ActorSystem();

try {
    new class extends Actor {public function receive(){}};
} catch (Error $e) {
    echo $e->getMessage();
}
ActorSystem::shutdown();
--EXPECT--
Actors cannot be created via class instantiation - create an ActorRef object instead
