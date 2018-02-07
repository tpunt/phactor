--TEST--
Ensure op array literals are properly copied
--DESCRIPTION--
Basic values don't need manual copying.
Strings are interned, so also don't need copying.
Arrays do need duplicating.
--FILE--
<?php

use phactor\{ActorSystem, Actor, function spawn};

$actorSystem = new ActorSystem(true);

class A
{
    public static function B()
    {
        return 'a';
        return 1.1;
        return 1;
        return null;
        return true;
        return false;
        return [];
        return $a;
    }
}

class Test extends Actor
{
    public function __construct()
    {
        $this->send($this, 1);
    }

    public function receive($sender, $message)
    {
        A::B();
        ActorSystem::shutdown();
    }
}

spawn('test', Test::class);
--EXPECT--
