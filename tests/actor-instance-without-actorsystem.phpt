--TEST--
Actor instance without ActorSystem
--DESCRIPTION--
Instantiate a new Actor object without the ActorSystem object.
--FILE--
<?php

try {
    new class extends Actor {
        public function receive($sender, $message) {}
    };
} catch (Error $e) {
    echo $e->getMessage();
}

--EXPECT--
The ActorSystem class must first be instantiated
