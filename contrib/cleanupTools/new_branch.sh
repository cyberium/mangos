#!/bin/sh
## Dialogue helper to create a "topic-branch" for cmangos

# Steps in interactive dialog:
# 1) Get name of contribution (ie LFG)
# 2) Get type of contribution (feature, fix, rewrite, cleanup)
# 3) Get short description of contribution
# 4) Get place where the topic should be published (own repo --> address)
# 5) Check current state of worktree: dirty/clean;
#     if dirty ask if changes should be taken into new topic
#     ELSE ask if topic should be forked from origin/master or current branch
# Switch to topic-branch (ie "feature_LFG")
# Create initial commit (including some suggested commit message
# 6) Edit the suggested commit message
# 7) Push to repo (check for ok)
# Create output to link to this initial commit from a sticky thread on the forum (address of commit, short description)
# And ask the user to publish this content to some sticky thread

echo "Welcome to CMaNGOS' helper tool to create a branch for a development project"
echo "This script will guide you through seven steps to set up a branch in which you can develop your project"
echo

# are we in git root dir?
if [[ ! -d .git/ ]]
then
  echo -e "ERROR: not in repository root directory"
  echo "This script must be started from a Git-root directory"
  echo "Usually it is started with \"sh contrib/create_topic_branch.sh\" "
  exit 1
fi

CHECKOUT_FROM_CURRENT=0
# is the index clean?
if [[ ! -z $(git diff-index HEAD) ]]
then
  echo "Your current working dir is not clean."
  echo "Do you want to take your changes over to your development project? (y/n)"
  read line
  echo
  if [[ "$line" = "y" ]]
  then
    CHECKOUT_FROM_CURRENT=1
  else
    echo -e "ERROR: dirty index, run mixed/hard reset first"
    exit 1
  fi
fi

echo "Please enter the name of your development project"
read PR_NAME
echo
echo "What type of contribution will your project be? (examples: feature, rewrite, cleanup, fix)"
read PR_TYPE
echo
echo "Please enter a short -oneline- description of your development project"
read PR_DESC
echo
echo "On which place do you intend to publish your contribution?"
echo "Currently you have these repositories created:"
echo
git remote
echo
echo "Please enter one of these git remotes"
read REMOTE
ADDRESS=$(git remote -v | grep "$REMOTE" | head -n1 | awk '{ print $2; }' | sed 's%.*github.com.\(.*\)\.git%\1%')
ADDRESS="https://github.com/${ADDRESS}"
echo
if [[ "$CHECKOUT_FROM_CURRENT" = "1" ]]
then
  git checkout -b ${PR_TYPE}_${PR_NAME}
else
  echo "From where do you want to checkout to your development branch?"
  echo "Default: origin/master - Use \"HEAD\" for current branch"
  read POINT
  if [[ "$POINT" = "" ]]
  then
    POINT="origin/master"
  fi
  git checkout -b ${PR_TYPE}_${PR_NAME} $POINT
fi
echo
echo "You will now asked to edit some initial commit message"
echo
echo "For this we will start your editor which might start vi, here are some information how to work with it:"
echo "Press \"i\" to start editing, Press ESC to stopp editing."
echo
echo "When you are finished editing, you can exit with [ESC] and \":wq\" "
echo
echo "You can exit the editor with [ESC] and \":wq\" !"
echo
echo "Press [ENTER] when you are ready to start the editor"
read line
echo
git commit -ea --allow-empty -m"Initial commit for development of $RP_TYPE $PR_NAME

Development branch for $PR_TYPE

        $PR_NAME

to do:
    $PR_DESC

This development project is published at:
    ${ADDRESS}/commits/${PR_TYPE}_${PR_NAME}

Please fill in some description about your project, these points might
help you to get some structure to your detailed description:

Overview:


Happy coding!
"
echo

echo "Do you want to directly publish this development branch?"
read line
echo
if [[ "$line" = "y" ]]
then
  HASH=`git log HEAD ^HEAD^ --pretty=format:"%hn"`
  #Push now
  git push $REMOTE ${PR_TYPE}_${PR_NAME}
  echo
  echo "Please link to your started project on our forum!"
  echo
  echo "Here is some suggested content for publishing on the http://cmangos.net forums at:"
  echo "    <LINK>"
  echo "or for a custom project:    <LINK2>"
  echo
  echo "Development of $PR_TYPE $PR_NAME - see [URL=${ADDRESS}/commit/$HASH]Initial commit[/URL]"
  echo "to develop: $PR_DESC"
  echo
  echo "In case of some contribution for official development, we also suggest to open a pull request for your branch"
  echo "<LINK2PULL>"
  echo
  echo "Note: On Windows you can use Copy&Paste from git-bash by clicking on the top-left corner, select \"Edit\" and then \"mark\""
  echo
fi
echo
echo "Have fun developing your feature!"
echo