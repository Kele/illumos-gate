BEGIN
{
	self->fop = "<badfop>";
	self->inside = 0;
}

::fsh_hook_install:entry
/ self->inside == 0 /
{
	printf("fsh_hook_install(op = %d, magic1 = %d) ",
	    ((fsht_int_t *)args[1]->arg)->fshti_arg.op,
	    ((fsht_int_t *)args[1]->arg)->fshti_arg.magic1);
}
::fsh_hook_install:return
/ self->inside == 0 /
{
	printf("= %d\n", args[1]);
}

::fsh_hook_install:entry
/ self->inside == 1 /
{
	printf("fsh_hook_install(EMPTY)\n");
}

::fsh_hook_remove:entry
/ self->inside == 0 /
{
	printf("fsh_hook_remove(%d)\n", args[0]);
}
::fsh_hook_remove:entry
/ self->inside == 1 /
{
	printf("fsh_hook_remove(EMPTY)\n");
}

::fsht_remove_cb:entry
/ self->inside == 0 /
{
	printf("fsht_remove_cb(%d)\n", args[1]);
	self->inside = 1;
}

::fsht_remove_cb:return
{
	self->inside = 0;
}


::fsh_read:entry
{
	self->fop = "read";
}

::fsh_read:return
{
	self->fop = "<badfop>";
}

:fshtest:pre_hook:entry
{
	this->fshti = (fsht_int_t *)args[0];
	this->fshtarg = &this->fshti->fshti_arg;

	printf("pre %s:\thandle = %d\n", self->fop, this->fshti->fshti_handle);

	self->inside = 1;
}

:fshtest:pre_hook:return
{
	self->inside = 0;
}

:fshtest:post_hook:entry
{
	this->fshti = (fsht_int_t *)args[0];
	this->fshtarg = &this->fshti->fshti_arg;
	
	printf("post %s:\thandle = %d\n", self->fop, this->fshti->fshti_handle);

	self->inside = 1;
}

:fshtest:post_hook:return
{
	self->inside = 0;
}
