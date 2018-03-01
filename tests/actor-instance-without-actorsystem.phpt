--TEST--
Actor instance without ActorSystem
--DESCRIPTION--
Instantiate a new Actor object without the ActorSystem object.
--FILE--
<?php

use phactor\{Actor, ActorRef};

try {
    new ActorRef(Actor::class);
} catch (Error $e) {
    echo $e->getMessage();
}
--EXPECT--
The ActorSystem class must first be instantiated
