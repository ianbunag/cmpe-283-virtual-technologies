locals {
  slug                   = "cmpe-283"
  domain                 = "${local.slug}.${var.root_domain}"
  git_repository         = "git@github.com:ianbunag/cmpe-283-virtual-technologies.git"
  git_email              = "cmpe-283@ianbunag.dev"
  git_name               = "CMPE-283 Lab VM"
  deploy_key_secret_name = "cmpe-283-ianbunag-dev-github-deploy-key"
}
