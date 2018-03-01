--TEST--
Ensure array literals with string elements are properly copied
--DESCRIPTION--
zend_array_dup does not perform a hard copy of array values, causing race
conditions on reference counts and ultimately heap corruption issues.
--FILE--
<?php

use phactor\{ActorSystem, Actor, ActorRef};

$actorSystem = new ActorSystem();

class Test extends Actor
{
    private $a = ['a', 'b', 'c', 'd', 'e', 'f'];

    public function receive() {ActorSystem::shutdown();}
}

new ActorRef(Test::class, [], 'test');
--EXPECT--
