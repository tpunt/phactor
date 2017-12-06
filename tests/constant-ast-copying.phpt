--TEST--
Ensure that the IS_CONSTANT_AST op array literal is properly copied.
--DESCRIPTION--
This requires a duplication of any zvals in the AST, as well as a new copy of
the zval ast value.
--FILE--
<?php

$actorSystem = new ActorSystem();

const Z = 1;

class A
{
    public static function B($a = Z, $b = Z . Z)
    {
        var_dump($a, $b);
    }
}

A::B();

$actorSystem->block();
--EXPECT--
int(1)
string(2) "11"
